#include "input_source.h"
#include "modal_input.h"
#include "controls.h"
#include "game_ui.h"
#include "screens.h"
#include "ui/highlight.h"
#include "platform/imports.h"

#include <cstring>

namespace enhancements {
namespace {
ImportHooks imports;
using Peek = decltype(&PeekMessageA);
Peek previous_peek = nullptr;
thread_local HWND owner = nullptr;
thread_local bool servicing = false;
thread_local ULONGLONG last_update = 0;

BOOL WINAPI peek_message(LPMSG message, HWND window, UINT minimum, UINT maximum, UINT remove) {
    // Native blue dialogs poll only mouse messages, bypassing the normal input timer.
    if (owner && !servicing && window == owner && minimum == WM_MOUSEFIRST &&
        maximum == WM_MBUTTONDBLCLK && (remove & PM_REMOVE)) {
        const auto now = GetTickCount64();
        if (now - last_update >= 16) {
            last_update = now;
            servicing = true;
            if (game::menu_confirmation_active()) {
                const bool focused = game_is_foreground(owner);
                if (focused) {
                    MSG key{};
                    while (PeekMessageW(&key, owner, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
                        if (key.message == WM_KEYDOWN) {
                            navigate_screen(owner,
                                            (key.wParam == VK_RIGHT) - (key.wParam == VK_LEFT),
                                            (key.wParam == VK_DOWN) - (key.wParam == VK_UP),
                                            key.wParam == VK_RETURN, key.wParam == VK_BACK,
                                            key.wParam == VK_TAB, true);
                        }
                    }
                }
                poll_controller(owner);
                update_highlight(owner, focused);
            }
            servicing = false;
        }
    }
    const auto result = previous_peek(message, window, minimum, maximum, remove);
    if (result && (remove & PM_REMOVE)) {
        observe_mouse_button(message->message);
    }
    return result;
}

FARPROC resolve(const char* name) {
    return std::strcmp(name, "PeekMessageA") == 0 ? reinterpret_cast<FARPROC>(&peek_message)
                                                  : nullptr;
}
}

void attach_modal_input(HWND window) {
    if (!game::executable_image() || imports.active()) {
        return;
    }
    // Display-mode changes may replace the executable's input imports.
    imports.remove();
    if (imports.install(GetModuleHandleW(nullptr), "USER32.dll", resolve)) {
        previous_peek =
            reinterpret_cast<Peek>(imports.previous(reinterpret_cast<FARPROC>(&peek_message)));
        owner = window;
    }
}

void detach_modal_input() {
    imports.remove();
    owner = nullptr;
}
}
