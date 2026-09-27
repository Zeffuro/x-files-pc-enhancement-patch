#pragma once
#include <windows.h>

namespace devtools {
inline void pump_tool_messages(HWND window) {
    if (!window) {
        return;
    }
    MSG message{};
    const auto deadline = GetTickCount64() + 3;
    for (unsigned count = 0;
         count < 64 && IsWindow(window) && PeekMessageW(&message, window, 0, 0, PM_NOREMOVE);
         ++count) {
        if (message.message == WM_QUIT) {
            break;
        }
        const auto id = message.message;
        if (!PeekMessageW(&message, window, id, id, PM_REMOVE)) {
            continue;
        }
        if (message.message == WM_QUIT) {
            PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (GetTickCount64() >= deadline) {
            break;
        }
    }
}
}
