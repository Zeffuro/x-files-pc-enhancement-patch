#include "output.h"
#include "volume.h"
#include "settings.h"
#include "runtime.h"

#include <xaudio2.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <stdexcept>

namespace playback {
namespace {

using Microsoft::WRL::ComPtr;

void check(HRESULT result) {
    if (FAILED(result)) {
        throw std::runtime_error("XAudio2 output failed (" + std::to_string(result) + ")");
    }
}

struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ~Apartment() {
        if (SUCCEEDED(result)) {
            CoUninitialize();
        }
    }
};

struct Engine {
    Apartment apartment;
    ComPtr<IXAudio2> audio;
    IXAudio2MasteringVoice* master = nullptr;
    std::wstring device = settings().audio_device;

    Engine() {
        check(XAudio2Create(&audio));
        auto result = audio->CreateMasteringVoice(
            &master, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0,
            device.empty() ? nullptr : device.c_str(), nullptr, AudioCategory_GameEffects);
        if (FAILED(result) && !device.empty()) {
            trace_value("audio_device_unavailable", static_cast<std::uint32_t>(result));
            result = audio->CreateMasteringVoice(&master);
        }
        check(result);
    }

    ~Engine() {
        if (master) {
            master->DestroyVoice();
        }
    }
};

std::shared_ptr<Engine> audio_engine() {
    static thread_local std::weak_ptr<Engine> cached;
    auto engine = cached.lock();
    if (!engine || engine->device != settings().audio_device) {
        engine = std::make_shared<Engine>();
        cached = engine;
    }
    return engine;
}

}

struct Output::State {
    std::shared_ptr<Engine> engine = audio_engine();
    IXAudio2SourceVoice* voice = nullptr;

    ~State() {
        if (voice) {
            voice->DestroyVoice();
        }
    }
};

Output::Output(const WAVEFORMATEX& format) : state_(std::make_unique<State>()) {
    check(state_->engine->audio->CreateSourceVoice(&state_->voice, &format));
}

Output::~Output() = default;

bool Output::current_device() const {
    return state_->engine->device == settings().audio_device;
}

void Output::play(std::span<const std::int16_t> samples) {
    stop();
    if (samples.empty()) {
        return;
    }
    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(samples.size_bytes());
    buffer.pAudioData = reinterpret_cast<const BYTE*>(samples.data());
    check(state_->voice->SubmitSourceBuffer(&buffer));
    check(state_->voice->Start());
}

void Output::stop() {
    check(state_->voice->Stop());
    check(state_->voice->FlushSourceBuffers());
}

void Output::volume(std::int16_t level, std::int16_t balance) {
    const auto gains = stereo_volume(level, balance);
    const float channels[]{static_cast<float>(gains & 0xffff) / 65535,
                           static_cast<float>(gains >> 16) / 65535};
    check(state_->voice->SetChannelVolumes(2, channels));
}

std::vector<OutputDevice> output_devices() {
    Apartment apartment;
    ComPtr<IMMDeviceEnumerator> enumerator;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&enumerator)));
    ComPtr<IMMDeviceCollection> devices;
    check(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices));
    UINT count = 0;
    check(devices->GetCount(&count));
    std::vector<OutputDevice> result{{L"", L"Windows default (automatic switching)"}};
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        ComPtr<IPropertyStore> properties;
        check(devices->Item(index, &device));
        check(device->OpenPropertyStore(STGM_READ, &properties));
        PROPVARIANT name{};
        check(properties->GetValue(PKEY_Device_FriendlyName, &name));
        LPWSTR id = nullptr;
        const auto status = device->GetId(&id);
        if (SUCCEEDED(status) && name.vt == VT_LPWSTR) {
            result.push_back({id, name.pwszVal});
        }
        CoTaskMemFree(id);
        PropVariantClear(&name);
        check(status);
    }
    return result;
}

}
