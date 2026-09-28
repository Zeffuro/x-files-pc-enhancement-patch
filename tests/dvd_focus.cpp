#include "dvd/focus.h"

#include <stdexcept>

namespace {
void require(bool value) {
    if (!value) {
        throw std::runtime_error("DVD focus barrier regression");
    }
}

void activate(playback::HeldFastForward& state) {
    require(!state.update(false, true));
    require(state.update(true, true));
}
}

int main() {
    const auto window =
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    require(window != nullptr);
    playback::HeldFastForward first, second;
    dvd::FocusBarrier a(first), b(second);
    a.attach(window);
    b.attach(window);
    constexpr UINT messages[]{WM_KILLFOCUS, WM_ACTIVATEAPP, WM_ENABLE, WM_CANCELMODE};
    for (const auto message : messages) {
        activate(first);
        activate(second);
        SendMessageW(window, message, FALSE, 0);
        SendMessageW(window, message == WM_KILLFOCUS ? WM_SETFOCUS : message, TRUE, 0);
        require(!first.update(true, true) && !second.update(true, true));
    }
    activate(first);
    SendMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0);
    require(!first.update(true, true));
    a.detach();
    activate(first);
    activate(second);
    SendMessageW(window, WM_KILLFOCUS, 0, 0);
    require(first.active() && !second.active());
    activate(second);
    DestroyWindow(window);
    require(!second.update(true, true));
}
