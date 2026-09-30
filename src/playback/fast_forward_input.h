#pragma once

#include "fast_forward.h"
#include <windows.h>
#include <xinput.h>

namespace playback {
inline void suspend_fast_forward_input() {
    ++movie_speed_input.context_epoch;
}

inline bool poll_controller_speed(HeldFastForward& state, bool enabled,
                                  const controller::Profile& requested,
                                  DWORD(WINAPI* read)(DWORD, XINPUT_STATE*) = XInputGetState) {
    const auto profile = controller::normalize(requested);
    state.profile(profile);
    bool down = false;
    const auto binding = profile.bindings[static_cast<std::size_t>(controller::Action::Speed)];
    for (unsigned index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE pad{};
        const bool connected = enabled && read(index, &pad) == ERROR_SUCCESS;
        const bool pressed =
            controller::held(binding, pad.Gamepad.wButtons, pad.Gamepad.bLeftTrigger,
                             pad.Gamepad.bRightTrigger, profile.trigger_threshold);
        down = state.controller(index, connected, pressed) || down;
    }
    return down;
}

inline bool poll_fast_forward(HeldFastForward& state, HWND window, unsigned key, bool gamepad,
                              bool allowed = true, const controller::Profile& profile = {}) {
    state.profile(profile);
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
    down = poll_controller_speed(state, gamepad, profile) || down;
    return state.update(down, key || gamepad);
}
}
