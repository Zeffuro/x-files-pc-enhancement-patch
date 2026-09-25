#include "input_source.h"
#include "controls.h"
#include "ui/settings_dialog.h"
#include "ui/menu_link.h"
#include "dialogue.h"
#include "inventory.h"
#include "menu.h"
#include "modal_input.h"
#include "screens.h"
#include "game_ui.h"
#include "login.h"
#include "ui/highlight.h"
#include "text_entry.h"
#include "quick_save.h"
#include "ui/notification.h"
#include "settings.h"

#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>

namespace enhancements {
namespace {

constexpr UINT_PTR subclass_id = 0x5846;
constexpr UINT settings_command = 0x1ff0;
constexpr UINT settings_message = WM_APP + 0x46;
thread_local HWND game_window = nullptr;
thread_local UINT_PTR timer = 0;
thread_local bool dialog_open = false;
thread_local bool f10_down = false;
thread_local bool escape_consumed = false;
thread_local bool right_click_consumed = false;
thread_local std::array<bool, 256> navigation_keys{};
thread_local HICON large_icon = nullptr;
thread_local HICON small_icon = nullptr;
thread_local HICON previous_large_icon = nullptr;
thread_local HICON previous_small_icon = nullptr;

void show_settings(HWND window) {
    if (!dialog_open) {
        dialog_open = true;
        update_settings_link(window, false);
        show_settings_dialog(window);
        dialog_open = false;
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR,
                             DWORD_PTR) {
    static bool settings_clicked = false;
    observe_mouse_button(message);
    if (message == WM_RBUTTONDOWN && !dialog_open && !text_entry_busy()) {
        right_click_consumed = close_dialogue(window);
        if (right_click_consumed) {
            return 0;
        }
    }
    if (message == WM_RBUTTONUP && right_click_consumed) {
        right_click_consumed = false;
        return 0;
    }
    if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) && !dialog_open &&
        settings_link_visible()) {
        POINT point{};
        // This subclass runs before cnc-ddraw scales mouse-message coordinates.
        const bool inside = GetCursorPos(&point) && ScreenToClient(window, &point) &&
                            PtInRect(&settings_link, point);
        if (message == WM_LBUTTONDOWN) {
            settings_clicked = inside;
        }
        if (settings_clicked) {
            if (message == WM_LBUTTONUP) {
                settings_clicked = false;
                if (inside) {
                    request_settings(window);
                }
            }
            return 0;
        }
    }
    if (text_entry_message(message, value, data)) {
        return 0;
    }
    if (value < navigation_keys.size() && navigation_keys[value]) {
        if (message == WM_CHAR) {
            return 0;
        }
        if (message == WM_KEYUP) {
            navigation_keys[value] = false;
            return 0;
        }
    }
    if ((message == WM_CHAR || message == WM_KEYUP) && value == VK_ESCAPE && escape_consumed) {
        return 0;
    }
    if (message == WM_KEYDOWN && !dialog_open && !text_entry_busy()) {
        if (value == VK_F5 || value == VK_F9) {
            if (!(data & (1L << 30)) && game_is_foreground(window)) {
                quick_save(window, value == VK_F9);
            }
            navigation_keys[value] = true;
            return 0;
        }
        const int horizontal = (value == VK_RIGHT) - (value == VK_LEFT);
        const int vertical = (value == VK_DOWN) - (value == VK_UP);
        if ((horizontal || vertical || value == VK_RETURN || value == VK_BACK || value == VK_TAB) &&
            navigate_screen(window, horizontal, vertical, value == VK_RETURN, value == VK_BACK,
                            value == VK_TAB, true)) {
            navigation_keys[value] = true;
            return 0;
        }
        if (value == VK_TAB && !(data & (1L << 30)) &&
            (focus_conversation_evidence(window) || navigate_emotions(window, 0, true) ||
             focus_inventory(window))) {
            return 0;
        }
        if (value == VK_BACK && (leave_inventory(window) || close_dialogue(window))) {
            return 0;
        }
        if (value == VK_ESCAPE) {
            if (game::menu_confirmation_active()) {
                escape_consumed = true;
                return 0;
            }
            if (data & (1L << 30)) {
                escape_consumed = true;
                return 0;
            }
            clear_dialogue();
            clear_inventory_focus();
            escape_consumed = resume_from_menu();
            if (escape_consumed) {
                return 0;
            }
        }
        const int direction = (value == VK_DOWN) - (value == VK_UP);
        const int tab = (value == VK_RIGHT) - (value == VK_LEFT);
        if (navigate_emotions(window, tab ? tab : direction, false)) {
            return 0;
        }
        if ((direction || tab) && navigate_inventory(window, tab)) {
            return 0;
        }
        if ((direction || tab) && navigate_dialogue(window, direction, tab)) {
            return 0;
        }
    }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && value == VK_F10) {
        if (!f10_down && !dialog_open) {
            PostMessageW(window, settings_message, 0, 0);
        }
        f10_down = true;
        return 0;
    }
    if ((message == WM_KEYUP || message == WM_SYSKEYUP) && value == VK_F10) {
        f10_down = false;
        return 0;
    }
    if (message == settings_message) {
        show_settings(window);
        return 0;
    }
    if (message == WM_SYSCOMMAND && (value & 0xfff0) == settings_command) {
        show_settings(window);
        return 0;
    }
    if (message == WM_TIMER && value == timer) {
        attach_modal_input(window);
        update_menu();
        update_quick_load();
        update_notification(window);
        const bool focused = game_is_foreground(window);
        update_settings_link(window, !dialog_open && !IsIconic(window));
        update_dialogue(focused && !dialog_open);
        update_login(window, focused && !dialog_open);
        update_highlight(window, focused && !dialog_open && !text_entry_busy());
        update_text_entry(window, focused && !dialog_open);
        const bool pressed = focused && (GetAsyncKeyState(VK_F10) & 0x8000);
        const bool open = pressed && !f10_down;
        f10_down = pressed;
        if (!dialog_open) {
            if (open) {
                show_settings(window);
            } else {
                poll_controller(window);
            }
        }
        return 0;
    }
    if (message == WM_NCDESTROY) {
        detach_controls();
    }
    return DefSubclassProc(window, message, value, data);
}

}

bool game_is_foreground(HWND window) {
    // cnc-ddraw reports the game as foreground through GetForegroundWindow even when inactive.
    GUITHREADINFO thread{sizeof(GUITHREADINFO)};
    return GetGUIThreadInfo(0, &thread) && thread.hwndActive == window;
}

void request_settings(HWND window) {
    PostMessageW(window, settings_message, 0, 0);
}

void attach_controls(HWND window) {
    if (game_window || !window) {
        return;
    }
    RECT client{};
    if (!GetClientRect(window, &client) || client.right < 640 || client.bottom < 480) {
        return;
    }
    if (SetWindowSubclass(window, window_proc, subclass_id, 0)) {
        game_window = window;
        attach_menu();
        attach_dialogue(window);
        attach_modal_input(window);
        timer = SetTimer(window, subclass_id, 16, nullptr);
        const auto menu = GetSystemMenu(window, FALSE);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, settings_command, L"Enhancements...\tF10");
        std::vector<wchar_t> path(32768);
        const auto length =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length && length < path.size() &&
            ExtractIconExW(path.data(), 0, &large_icon, &small_icon, 1)) {
            previous_large_icon = reinterpret_cast<HICON>(
                SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon)));
            previous_small_icon = reinterpret_cast<HICON>(
                SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon)));
        }
    }
}

void detach_controls() {
    detach_modal_input();
    release_settings_link();
    release_text_entry();
    release_highlight();
    release_notification();
    clear_inventory_focus();
    detach_dialogue();
    if (game_window && IsWindow(game_window)) {
        KillTimer(game_window, timer);
        RemoveWindowSubclass(game_window, window_proc, subclass_id);
        SendMessageW(game_window, WM_SETICON, ICON_BIG,
                     reinterpret_cast<LPARAM>(previous_large_icon));
        SendMessageW(game_window, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(previous_small_icon));
    }
    if (large_icon) {
        DestroyIcon(large_icon);
    }
    if (small_icon) {
        DestroyIcon(small_icon);
    }
    large_icon = small_icon = previous_large_icon = previous_small_icon = nullptr;
    game_window = nullptr;
    timer = 0;
}

}
