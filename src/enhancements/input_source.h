#pragma once

#include <windows.h>

namespace enhancements {
inline constexpr ULONG_PTR controller_event = 0x58464354;
inline thread_local bool controller_active = false;
inline thread_local POINT last_pointer{};
inline thread_local bool pointer_known = false;

inline void observe_pointer() {
    POINT current{};
    if (GetCursorPos(&current)) {
        if (pointer_known && (current.x != last_pointer.x || current.y != last_pointer.y)) {
            controller_active = false;
        }
        last_pointer = current;
        pointer_known = true;
    }
}

inline BOOL move_controller_pointer(int x, int y) {
    const auto moved = SetCursorPos(x, y);
    pointer_known = GetCursorPos(&last_pointer) != FALSE;
    return moved;
}

inline void observe_mouse_button(UINT message) {
    if ((message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
         message == WM_MOUSEWHEEL) &&
        static_cast<ULONG_PTR>(GetMessageExtraInfo()) != controller_event) {
        controller_active = false;
    }
}
}
