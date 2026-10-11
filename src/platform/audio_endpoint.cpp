#include "audio_endpoint.h"

namespace platform {

AudioEndpoint::AudioEndpoint()
    : apartment_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), result_(apartment_) {
    if (FAILED(apartment_) && apartment_ != RPC_E_CHANGED_MODE) {
        return;
    }
    result_ = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&enumerator_));
    if (FAILED(result_)) {
        return;
    }
    if (!enumerator_) {
        result_ = E_POINTER;
        return;
    }
    // Wine's XAudio2 can dereference a null endpoint when the default is missing.
    result_ = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint_);
    if (SUCCEEDED(result_) && !endpoint_) {
        result_ = E_POINTER;
    }
}

AudioEndpoint::~AudioEndpoint() {
    endpoint_.Reset();
    enumerator_.Reset();
    if (SUCCEEDED(apartment_)) {
        CoUninitialize();
    }
}

}
