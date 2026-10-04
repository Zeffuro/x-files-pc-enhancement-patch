#pragma once

#include <windows.h>
#include <array>

namespace enhancements {
class NavigationKeys {
public:
    void consume(WPARAM key) {
        if (key < keys.size()) {
            keys[key] = true;
        }
    }

    bool owns(UINT message, WPARAM key, LPARAM data) {
        if (key >= keys.size() || !keys[key]) {
            return false;
        }
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(data & (1L << 30))) {
            keys[key] = false;
            return false;
        }
        if (message == WM_KEYUP || message == WM_SYSKEYUP) {
            keys[key] = false;
            return true;
        }
        if (message == WM_CHAR || message == WM_SYSCHAR) {
            return key == VK_RETURN || key == VK_TAB || key == VK_BACK;
        }
        return (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && (data & (1L << 30));
    }

    void reset() {
        keys.fill(false);
    }

private:
    std::array<bool, 256> keys{};
};

bool navigate_keyboard(HWND window, WPARAM key);
}
