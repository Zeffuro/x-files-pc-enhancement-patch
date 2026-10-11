#include "playback/output.h"
#include "platform/audio_endpoint.h"
#include "settings.h"

#include <xaudio2.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

extern unsigned missing_endpoint_constructions;
extern unsigned missing_endpoint_destructions;

namespace {
Settings configured;
unsigned xaudio_calls = 0;
unsigned unavailable_traces = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

HRESULT WINAPI forbidden_xaudio_create(IXAudio2**, UINT32, XAUDIO2_PROCESSOR) {
    ++xaudio_calls;
    return E_UNEXPECTED;
}

void exercise(playback::Output& output) {
    const std::array<std::int16_t, 8> samples{1, -1, 2, -2, 3, -3, 4, -4};
    output.stop();
    output.play({});
    output.play(samples);
    output.volume(256, 0);
    output.volume(0, -128);
    output.volume(128, 127);
    for (unsigned speed = 1; speed <= 4; ++speed) {
        output.speed(speed);
    }
    output.stop();
}
}

// Intercept the SDK import without loading an audio DLL or using a real endpoint.
extern "C" {
HRESULT(WINAPI* forbidden_xaudio_create_import)(IXAudio2**, UINT32,
                                                XAUDIO2_PROCESSOR) = forbidden_xaudio_create;
}
#ifdef _M_IX86
#pragma comment(linker, "/alternatename:__imp__XAudio2Create@12=_forbidden_xaudio_create_import")
#else
#pragma comment(linker, "/alternatename:__imp_XAudio2Create=forbidden_xaudio_create_import")
#endif

const Settings& settings() {
    return configured;
}

void trace_value(const char* name, std::uint32_t value) {
    require(std::string_view(name) == "audio_endpoint_unavailable",
            "Unexpected trace while audio endpoint is missing");
    require(value == static_cast<std::uint32_t>(E_NOTFOUND), "Missing endpoint HRESULT changed");
    ++unavailable_traces;
}

int main() {
    try {
        WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
        configured.audio_device = L"requested-device";
        {
            playback::Output first(format);
            require(missing_endpoint_constructions == 1, "Initial endpoint was not checked once");
            for (unsigned frame = 0; frame < 100; ++frame) {
                require(first.current_device(), "Unavailable output lost requested device");
                exercise(first);
            }
            require(missing_endpoint_constructions == 1, "Output retried audio on each frame");
            for (unsigned clip = 0; clip < 10; ++clip) {
                playback::Output shared(format);
                exercise(shared);
                require(shared.current_device(), "Shared output lost requested device");
            }
            require(missing_endpoint_constructions == 11,
                    "New outputs did not retry while another unavailable output remained alive");
            configured.audio_device = L"changed-device";
            require(!first.current_device(), "Explicit device change was not detected");
            playback::Output changed(format);
            exercise(changed);
            require(changed.current_device(), "New output did not retain changed device");
            require(missing_endpoint_constructions == 12, "Explicit change did not retry endpoint");
        }
        require(missing_endpoint_destructions == 12, "Unavailable engines did not release state");
        for (unsigned clip = 0; clip < 10; ++clip) {
            playback::Output fresh(format);
            exercise(fresh);
        }
        require(missing_endpoint_constructions == 22, "Fresh outputs did not retry endpoint");
        require(missing_endpoint_destructions == 22, "Fresh endpoint state leaked");
        require(unavailable_traces == 22, "Missing endpoint trace count changed");
        require(xaudio_calls == 0, "XAudio2 was called with a missing default endpoint");
        std::cout << "Missing endpoint playback stays inert and retries only on new engines\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
