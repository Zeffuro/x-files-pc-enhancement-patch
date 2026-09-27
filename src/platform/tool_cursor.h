#pragma once

#include <windows.h>

namespace platform {
inline void tool_cursor(HWND game, bool enabled) {
    SendMessageW(game, RegisterWindowMessageW(L"XFilesEnhancement.ToolCursor"), enabled, 0);
}

class ToolCursor {
public:
    explicit ToolCursor(HWND game) : game_(game) {
        SendMessageW(game_, RegisterWindowMessageW(L"XFilesEnhancement.ToolCursor"), 3, 0);
    }

    ~ToolCursor() {
        SendMessageW(game_, RegisterWindowMessageW(L"XFilesEnhancement.ToolCursor"), 4, 0);
    }

    ToolCursor(const ToolCursor&) = delete;
    ToolCursor& operator=(const ToolCursor&) = delete;

private:
    HWND game_;
};
}
