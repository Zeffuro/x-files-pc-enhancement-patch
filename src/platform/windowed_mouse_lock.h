#pragma once

#include <windows.h>

namespace platform {
inline void windowed_mouse_lock(HWND game, bool enabled, bool paused = false) {
    SendMessageW(game, RegisterWindowMessageW(L"XFilesEnhancement.WindowedMouseLock"),
                 enabled ? 2 : 1, paused);
}
}
