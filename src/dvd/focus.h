#pragma once

#include "playback/fast_forward.h"
#include <windows.h>
#include <commctrl.h>
#include <stdexcept>

namespace dvd {
class FocusBarrier {
public:
    explicit FocusBarrier(playback::HeldFastForward& state) : state_(state) {}

    ~FocusBarrier() {
        detach();
    }

    FocusBarrier(const FocusBarrier&) = delete;
    FocusBarrier& operator=(const FocusBarrier&) = delete;

    void attach(HWND window) {
        if (window_ && window_ != window) {
            state_.reset();
        }
        detach();
        if (!SetWindowSubclass(window, messages, reinterpret_cast<UINT_PTR>(this),
                               reinterpret_cast<DWORD_PTR>(this))) {
            throw std::runtime_error("Cannot observe DVD playback focus");
        }
        window_ = window;
    }

    void detach() {
        if (window_) {
            RemoveWindowSubclass(window_, messages, reinterpret_cast<UINT_PTR>(this));
            window_ = nullptr;
        }
        state_.clear();
    }

private:
    static LRESULT CALLBACK messages(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR,
                                     DWORD_PTR reference) {
        auto& self = *reinterpret_cast<FocusBarrier*>(reference);
        if (message == WM_KILLFOCUS || message == WM_CANCELMODE ||
            (message == WM_ACTIVATEAPP && !value) || (message == WM_ENABLE && !value) ||
            (message == WM_KEYDOWN && value == VK_ESCAPE)) {
            self.state_.reset();
        }
        if (message == WM_NCDESTROY) {
            self.state_.reset();
            self.detach();
        }
        return DefSubclassProc(window, message, value, data);
    }

    playback::HeldFastForward& state_;
    HWND window_ = nullptr;
};
}
