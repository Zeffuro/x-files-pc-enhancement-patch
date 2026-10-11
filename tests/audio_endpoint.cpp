#include "platform/audio_endpoint.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Scenario {
    HRESULT apartment = S_OK;
    HRESULT creation = S_OK;
    HRESULT lookup = S_OK;
    bool null_enumerator = false;
    bool null_device = false;
    HRESULT expected = S_OK;
    const char* events = "ICGDEU";
};

Scenario scenario;
std::string events;
bool release_error = false;

struct Device final : IMMDevice {
    ULONG references = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override {
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        events += 'D';
        release_error |= references != 1;
        return references ? --references : 0;
    }

    HRESULT STDMETHODCALLTYPE Activate(REFIID, DWORD, PROPVARIANT*, void**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD, IPropertyStore**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetId(LPWSTR*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetState(DWORD*) override {
        return E_NOTIMPL;
    }
} device;

struct Enumerator final : IMMDeviceEnumerator {
    ULONG references = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override {
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        events += 'E';
        release_error |= references != 1;
        return references ? --references : 0;
    }

    HRESULT STDMETHODCALLTYPE EnumAudioEndpoints(EDataFlow, DWORD, IMMDeviceCollection**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetDefaultAudioEndpoint(EDataFlow flow, ERole role,
                                                      IMMDevice** result) override {
        require(flow == eRender && role == eConsole, "Wrong default endpoint role");
        require(references == 1 && result != nullptr, "Enumerator lookup lifetime changed");
        events += 'G';
        *result = nullptr;
        if (SUCCEEDED(scenario.lookup) && !scenario.null_device) {
            device.AddRef();
            *result = &device;
        }
        return scenario.lookup;
    }

    HRESULT STDMETHODCALLTYPE GetDevice(LPCWSTR, IMMDevice**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE
    RegisterEndpointNotificationCallback(IMMNotificationClient*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE
    UnregisterEndpointNotificationCallback(IMMNotificationClient*) override {
        return E_NOTIMPL;
    }
} enumerator;

HRESULT WINAPI initialize(void* reserved, DWORD flags) {
    require(reserved == nullptr && flags == COINIT_MULTITHREADED, "Wrong apartment arguments");
    events += 'I';
    return scenario.apartment;
}

HRESULT WINAPI create(REFCLSID clsid, IUnknown* outer, DWORD context, REFIID iid, void** result) {
    require(clsid == __uuidof(MMDeviceEnumerator) && iid == __uuidof(IMMDeviceEnumerator) &&
                outer == nullptr && context == CLSCTX_INPROC_SERVER && result != nullptr,
            "Wrong enumerator creation arguments");
    events += 'C';
    *result = nullptr;
    if (SUCCEEDED(scenario.creation) && !scenario.null_enumerator) {
        enumerator.AddRef();
        *result = &enumerator;
    }
    return scenario.creation;
}

void WINAPI uninitialize() {
    events += 'U';
    release_error |= device.references != 0 || enumerator.references != 0;
}
}

// Keep the production preflight while controlling its three COM imports.
extern "C" {
HRESULT(WINAPI* endpoint_initialize_import)(void*, DWORD) = initialize;
HRESULT(WINAPI* endpoint_create_import)(REFCLSID, IUnknown*, DWORD, REFIID, void**) = create;
void(WINAPI* endpoint_uninitialize_import)() = uninitialize;
}
#ifdef _M_IX86
#pragma comment(linker, "/alternatename:__imp__TestCoInitializeEx@8=_endpoint_initialize_import")
#pragma comment(linker, "/alternatename:__imp__TestCoCreateInstance@20=_endpoint_create_import")
#pragma comment(linker, "/alternatename:__imp__TestCoUninitialize@0=_endpoint_uninitialize_import")
#else
#pragma comment(linker, "/alternatename:__imp_TestCoInitializeEx=endpoint_initialize_import")
#pragma comment(linker, "/alternatename:__imp_TestCoCreateInstance=endpoint_create_import")
#pragma comment(linker, "/alternatename:__imp_TestCoUninitialize=endpoint_uninitialize_import")
#endif

int main() {
    try {
        const std::array cases{
            Scenario{},
            Scenario{S_FALSE},
            Scenario{RPC_E_CHANGED_MODE, S_OK, S_OK, false, false, S_OK, "ICGDE"},
            Scenario{E_ACCESSDENIED, S_OK, S_OK, false, false, E_ACCESSDENIED, "I"},
            Scenario{S_OK, E_FAIL, S_OK, false, false, E_FAIL, "ICU"},
            Scenario{S_OK, S_OK, S_OK, true, false, E_POINTER, "ICU"},
            Scenario{S_OK, S_OK, E_NOTFOUND, false, false, E_NOTFOUND, "ICGEU"},
            Scenario{S_OK, S_OK, S_OK, false, true, E_POINTER, "ICGEU"},
            Scenario{RPC_E_CHANGED_MODE, S_OK, E_NOTFOUND, false, false, E_NOTFOUND, "ICGE"},
        };
        for (const auto& test : cases) {
            scenario = test;
            events.clear();
            release_error = false;
            {
                platform::AudioEndpoint endpoint;
                const auto expected_calls =
                    FAILED(test.apartment) && test.apartment != RPC_E_CHANGED_MODE ? "I"
                    : FAILED(test.creation) || test.null_enumerator                ? "IC"
                                                                                   : "ICG";
                require(events == expected_calls, "Preflight skipped or repeated COM calls");
                for (unsigned query = 0; query < 10; ++query) {
                    require(endpoint.result() == test.expected, "Preflight HRESULT changed");
                    require(endpoint.available() == SUCCEEDED(test.expected),
                            "Preflight availability changed");
                }
                require(events == expected_calls, "Availability query repeated COM calls");
                require(device.references == (SUCCEEDED(test.expected) ? 1u : 0u),
                        "Endpoint interface was not retained");
                require(enumerator.references == (events == "ICG" ? 1u : 0u),
                        "Enumerator interface was not retained");
            }
            require(events == test.events, "COM release or apartment balance changed");
            require(!release_error && device.references == 0 && enumerator.references == 0,
                    "COM interfaces leaked or released after apartment shutdown");
        }
        std::cout << "Audio endpoint preflight COM failures and lifetime balance passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
