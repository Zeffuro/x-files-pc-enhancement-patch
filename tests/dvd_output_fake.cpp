#include "dvd/output.h"
#include "dvd/input.h"
#include "media/tempo.h"
#include <cstring>
#include <vector>

#include <algorithm>
extern "C" {
#include <libavutil/frame.h>
}

namespace dvd {
bool poll_speed(playback::HeldFastForward& state, HWND, unsigned key, bool,
                const controller::Profile& profile) {
    state.profile(profile);
    const bool down = GetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr, 0) != 0;
    const bool focus = GetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_UNFOCUSED", nullptr, 0) == 0;
    return state.update(down, key && focus);
}

struct Output::State {
    HWND window = nullptr;
    bool paused = false;
    unsigned speed = 1;
    bool underrun = GetEnvironmentVariableW(L"XFILES_DVD_TEST_UNDERRUN", nullptr, 0) != 0;
    std::int64_t samples = 0;
    std::unique_ptr<media::Tempo> tempo;
    std::int64_t elapsed = 0;
    ULONGLONG start = GetTickCount64();

    std::int64_t time() {
        const auto now = GetTickCount64();
        if (!paused) {
            elapsed = std::min(elapsed + static_cast<std::int64_t>(now - start) * 90 * speed,
                               samples * 90000 / 48000);
        }
        start = now;
        return elapsed;
    }
};

Output::Output(HWND parent) : state_(std::make_unique<State>()) {
    state_->window = parent;
}

Output::~Output() = default;

void Output::rectangles(RECT, RECT, RECT) {}

void Output::video(const AVFrame&) {}

void Output::caption(std::wstring text, const CaptionStyle&) {
    SetWindowTextW(state_->window, text.c_str());
}

void Output::audio(const AVFrame& frame, int first, int count) {
    state_->time();
    if (state_->speed > 1) {
        if (!state_->tempo) {
            state_->tempo = std::make_unique<media::Tempo>(48000, 2, state_->speed);
        }
        std::vector<std::int16_t> pcm(static_cast<std::size_t>(count) * 2);
        if (frame.format == AV_SAMPLE_FMT_S16) {
            std::memcpy(pcm.data(), frame.extended_data[0] + first * 4, pcm.size() * 2);
        } else {
            const auto* input = reinterpret_cast<const std::int32_t*>(frame.extended_data[0]);
            for (std::size_t i = 0; i < pcm.size(); ++i) {
                pcm[i] = static_cast<std::int16_t>(input[first * 2 + i] >> 16);
            }
        }
        state_->samples +=
            static_cast<std::int64_t>(state_->tempo->push(pcm).size() / 2) * state_->speed;
    } else {
        state_->samples += count;
    }
    SetPropW(state_->window, L"XFilesDvdTestAudio", reinterpret_cast<HANDLE>(1));
}

void Output::finish_audio() {
    if (state_->tempo) {
        state_->samples +=
            static_cast<std::int64_t>(state_->tempo->finish().size() / 2) * state_->speed;
    }
}

bool Output::drained() {
    if (GetEnvironmentVariableW(L"XFILES_DVD_TEST_DRAIN_PENDING", nullptr, 0)) {
        return false;
    }
    return state_->underrun || played() >= state_->samples * 90000 / 48000;
}

void Output::pause(bool value) {
    state_->elapsed = state_->time();
    state_->start = GetTickCount64();
    state_->paused = value;
}

void Output::speed(unsigned multiplier) {
    state_->time();
    state_->speed = multiplier;
    SetPropW(state_->window, L"XFilesDvdTestSpeed", reinterpret_cast<HANDLE>(multiplier));
}

void Output::clear() {
    discard_audio();
    SetWindowTextW(state_->window, L"");
}

void Output::discard_audio() {
    state_->tempo.reset();
    state_->samples = state_->elapsed = 0;
    state_->start = GetTickCount64();
    RemovePropW(state_->window, L"XFilesDvdTestAudio");
}

void Output::show(bool) {}

HWND Output::window() const {
    return state_->window;
}

std::int64_t Output::played() const {
    if (state_->underrun) {
        return 0;
    }
    return std::min(state_->time(), state_->samples * 90000 / 48000);
}
}
