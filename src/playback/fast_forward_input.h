#pragma once

#include "fast_forward.h"
#include <windows.h>
#include <xinput.h>

namespace playback {
inline void suspend_fast_forward_input() {
    ++movie_speed_input.context_epoch;
}

inline bool poll_fast_forward(HeldFastForward& state, HWND window, unsigned key, bool gamepad,
                              bool allowed = true) {
    state.context();
    GUITHREADINFO thread{sizeof(GUITHREADINFO)};
    const bool focused = window && GetGUIThreadInfo(0, &thread) && thread.hwndActive == window &&
                         IsWindowEnabled(window) && !IsIconic(window);
    if (!focused) {
        return state.update(false, false);
    }
    if (!allowed) {
        state.clear();
        return false;
    }
    bool down = key && (GetAsyncKeyState(static_cast<int>(key)) & 0x8000);
    for (unsigned index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE pad{};
        const bool connected = gamepad && XInputGetState(index, &pad) == ERROR_SUCCESS;
        const bool pressed = (pad.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        down = state.controller(index, connected, pressed) || down;
    }
    return state.update(down, key || gamepad);
}
}
