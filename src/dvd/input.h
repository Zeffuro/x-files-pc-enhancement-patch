#pragma once

#include "playback/fast_forward.h"
#include <windows.h>

namespace dvd {
inline playback::FastForwardInput& speed_input() {
    // The two playback DLLs share release barriers across clip transitions.
    const auto module = GetModuleHandleW(L"QuickTime.qts");
    const auto shared = module ? reinterpret_cast<playback::FastForwardInput*(__cdecl*)()>(
                                     GetProcAddress(module, "XFilesMovieSpeedInputV1"))
                               : nullptr;
    return shared ? *shared() : playback::movie_speed_input;
}

bool poll_speed(playback::HeldFastForward& state, HWND owner, unsigned key, bool gamepad);
}
