#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace platform {

class AudioEndpoint {
public:
    AudioEndpoint();
    ~AudioEndpoint();
    AudioEndpoint(const AudioEndpoint&) = delete;
    AudioEndpoint& operator=(const AudioEndpoint&) = delete;

    bool available() const {
        return SUCCEEDED(result_) && endpoint_.Get() != nullptr;
    }

    HRESULT result() const {
        return result_;
    }

private:
    HRESULT apartment_;
    HRESULT result_;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<IMMDevice> endpoint_;
};

}
