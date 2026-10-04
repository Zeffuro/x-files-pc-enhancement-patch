#include "audio.h"
#include "media/tempo.h"
#include "platform/test_environment.h"

#include <atomic>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
#include <xaudio2.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace dvd {
namespace {
void check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw std::runtime_error(std::string(operation) + " failed (" +
                                 std::to_string(static_cast<unsigned long>(result)) + ")");
    }
}

void check(int result, const char* operation) {
    if (result < 0) {
        throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(result) +
                                 ")");
    }
}

constexpr std::size_t max_audio_bytes = 4 * 1024 * 1024;
constexpr std::size_t max_audio_buffers = 48;

struct AudioCallback final : IXAudio2VoiceCallback {
    std::atomic<std::uint64_t> completed{0};
    std::atomic<HRESULT> error{S_OK};

    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}

    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}

    void STDMETHODCALLTYPE OnStreamEnd() override {}

    void STDMETHODCALLTYPE OnBufferStart(void*) override {}

    void STDMETHODCALLTYPE OnBufferEnd(void*) override {
        completed.fetch_add(1, std::memory_order_release);
    }

    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}

    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT result) override {
        error.store(result, std::memory_order_release);
    }
};
}

struct Audio::State {
    ~State() {
        reset_audio();
    }

    void ensure_audio();
    void reclaim();
    void reset_audio() noexcept;
    void submit(std::vector<std::int16_t> pcm);

    SwrContext* resampler = nullptr;
    Microsoft::WRL::ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* voice = nullptr;
    AudioCallback callback;
    std::deque<std::unique_ptr<std::vector<std::int16_t>>> buffers;
    std::size_t queued_bytes = 0;
    std::uint64_t reclaimed = 0;
    bool audio_apartment = false;
    bool paused = false;
    unsigned speed = 1;
    std::unique_ptr<media::Tempo> tempo;
    bool finished = false;
};

void Audio::State::ensure_audio() {
    if (voice) {
        return;
    }
    if (!engine) {
        const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
            check(apartment, "Audio apartment");
        }
        audio_apartment = SUCCEEDED(apartment);
        check(XAudio2Create(&engine), "XAudio2 engine");
        check(engine->CreateMasteringVoice(&master), "XAudio2 mastering voice");
        if (platform::test_audio_muted()) {
            check(master->SetVolume(0), "Mute DVD test audio");
        }
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 48000 * format.nBlockAlign;
    callback.error.store(S_OK);
    check(engine->CreateSourceVoice(&voice, &format, 0, 4.0f, &callback), "XAudio2 source voice");
    check(voice->SetFrequencyRatio(1.0f), "DVD audio speed");
    if (!paused) {
        check(voice->Start(), "Start DVD audio");
    }
}

void Audio::State::reclaim() {
    if (!voice) {
        return;
    }
    check(callback.error.load(std::memory_order_acquire), "DVD audio voice");
    const auto completed = callback.completed.load(std::memory_order_acquire);
    while (reclaimed < completed) {
        queued_bytes -= buffers.front()->size() * sizeof(std::int16_t);
        buffers.pop_front();
        ++reclaimed;
    }
}

void Audio::State::reset_audio() noexcept {
    if (voice) {
        voice->Stop();
        voice->FlushSourceBuffers();
        voice->DestroyVoice();
        voice = nullptr;
    }
    tempo.reset();
    finished = false;
    swr_free(&resampler);
    buffers.clear();
    queued_bytes = 0;
    callback.completed.store(0);
    reclaimed = 0;
    callback.error.store(S_OK);
    if (master) {
        master->DestroyVoice();
        master = nullptr;
    }
    engine.Reset();
    if (audio_apartment) {
        CoUninitialize();
        audio_apartment = false;
    }
}

void Audio::State::submit(std::vector<std::int16_t> pcm) {
    if (pcm.empty()) {
        return;
    }
    reclaim();
    const auto bytes = pcm.size() * sizeof(std::int16_t);
    if (bytes > max_audio_bytes || buffers.size() >= max_audio_buffers ||
        queued_bytes > max_audio_bytes - bytes) {
        throw std::runtime_error("DVD audio queue is full");
    }
    ensure_audio();
    buffers.push_back(std::make_unique<std::vector<std::int16_t>>(std::move(pcm)));
    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(bytes);
    buffer.pAudioData = reinterpret_cast<const BYTE*>(buffers.back()->data());
    const auto result = voice->SubmitSourceBuffer(&buffer);
    if (FAILED(result)) {
        buffers.pop_back();
        check(result, "Submit DVD audio");
    }
    queued_bytes += bytes;
}

Audio::Audio() : state_(std::make_unique<State>()) {}

Audio::~Audio() = default;

void Audio::push(const AVFrame& frame, int first, int count) {
    if (frame.sample_rate != 48000 || frame.ch_layout.nb_channels != 2 ||
        (frame.format != AV_SAMPLE_FMT_S16 && frame.format != AV_SAMPLE_FMT_S32) || first < 0 ||
        count < 0 || first > frame.nb_samples || count > frame.nb_samples - first ||
        !frame.extended_data || !frame.extended_data[0] || state_->finished) {
        throw std::runtime_error("Unsupported DVD audio frame");
    }
    if (!count) {
        return;
    }
    const auto bytes = static_cast<std::size_t>(count) * 4;
    if (bytes > max_audio_bytes) {
        throw std::runtime_error("DVD audio frame is too large");
    }
    std::vector<std::int16_t> pcm(static_cast<std::size_t>(count) * 2);
    if (frame.format == AV_SAMPLE_FMT_S16) {
        std::memcpy(pcm.data(), frame.extended_data[0] + static_cast<std::size_t>(first) * 4,
                    bytes);
    } else {
        if (!state_->resampler) {
            check(swr_alloc_set_opts2(&state_->resampler, &frame.ch_layout, AV_SAMPLE_FMT_S16,
                                      48000, &frame.ch_layout, AV_SAMPLE_FMT_S32, 48000, 0,
                                      nullptr),
                  "DVD audio converter");
            check(swr_init(state_->resampler), "Initialize DVD audio converter");
        }
        auto* output = reinterpret_cast<std::uint8_t*>(pcm.data());
        const auto* input = frame.extended_data[0] + static_cast<std::size_t>(first) * 8;
        const auto converted = swr_convert(state_->resampler, &output, count, &input, count);
        check(converted, "Convert DVD audio");
        if (converted != count) {
            throw std::runtime_error("DVD audio converter delayed samples");
        }
    }
    if (state_->speed > 1) {
        if (!state_->tempo) {
            state_->tempo = std::make_unique<media::Tempo>(48000, 2, state_->speed);
        }
        pcm = state_->tempo->push(pcm);
    }
    state_->submit(std::move(pcm));
}

void Audio::finish() {
    if (!state_->finished) {
        if (state_->tempo) {
            state_->submit(state_->tempo->finish());
        }
        state_->finished = true;
    }
}

bool Audio::drained() {
    state_->reclaim();
    return state_->buffers.empty();
}

std::int64_t Audio::played() const {
    if (!state_->voice) {
        return 0;
    }
    XAUDIO2_VOICE_STATE status{};
    state_->voice->GetState(&status);
    // Tempo output frames map back to decoded source time.
    const auto samples = status.SamplesPlayed * state_->speed;
    return static_cast<std::int64_t>((samples / 8) * 15 + (samples % 8) * 15 / 8);
}

void Audio::pause(bool paused) {
    if (state_->paused != paused) {
        if (state_->voice) {
            check(paused ? state_->voice->Stop() : state_->voice->Start(),
                  paused ? "Pause DVD audio" : "Resume DVD audio");
        }
        state_->paused = paused;
    }
}

void Audio::speed(unsigned multiplier) {
    if (multiplier < 1 || multiplier > 4) {
        throw std::runtime_error("Invalid DVD audio speed");
    }
    if (state_->speed != multiplier) {
        state_->reset_audio();
        state_->speed = multiplier;
    }
}

void Audio::clear() {
    state_->reset_audio();
}
}
