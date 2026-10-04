#include "input_source.h"
#include "controller_state.h"
#include "controller_click.h"
#include "rumble.h"
#include "controls.h"
#include "keyboard_navigation.h"
#include "ui/settings_dialog.h"
#include "ui/quick_menu.h"
#include "ui/menu_link.h"
#include "dialogue.h"
#include "hotspot_reveal.h"
#include "scrolling.h"
#include "documents.h"
#include "inventory.h"
#include "menu.h"
#include "modal_input.h"
#include "screens.h"
#include "game_ui.h"
#include "login.h"
#include "ui/highlight.h"
#include "ui/controller_hints.h"
#include "text_entry.h"
#include "quick_save.h"
#include "autosave.h"
#include "ui/continue_menu.h"
#include "ui/notification.h"
#include "settings.h"
#include "devtools/inspector.h"
#include "diagnostics/game_context.h"
#include "saves/browser.h"
#include "game/render/native_render.h"
#include "playback/fast_forward_input.h"
#include "transcript/view.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cstdint>

namespace enhancements {
namespace {

constexpr UINT_PTR subclass_id = 0x5846;
constexpr UINT settings_command = 0x1ff0;
constexpr UINT inspector_command = 0x1fe0;
constexpr UINT settings_message = WM_APP + 0x46;
thread_local HWND game_window = nullptr;
thread_local UINT_PTR timer = 0;
thread_local bool dialog_open = false;
thread_local bool f10_down = false;
thread_local bool escape_consumed = false;
thread_local bool right_click_consumed = false;
thread_local NavigationKeys navigation_keys;
thread_local NavigationKeys native_navigation_keys;
thread_local HICON large_icon = nullptr;
thread_local HICON small_icon = nullptr;
thread_local HICON previous_large_icon = nullptr;
thread_local HICON previous_small_icon = nullptr;

struct InventoryClick {
    HWND window = nullptr;
    POINT scene_cursor{};
    POINT item_cursor{};
    ULONGLONG queued_at = 0;
    std::uint64_t generation = 0;
    bool scene_known = false;
    input::ClickDispatch dispatch;
};

thread_local InventoryClick inventory_click;
thread_local std::uint64_t next_inventory_click_generation = 0;

void begin_inventory_mouse_dispatch(HWND window, UINT message, bool injected,
                                    std::uint64_t generation) {
    if (inventory_click.window == window && inventory_click.generation == generation && injected &&
        (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)) {
        inventory_click.dispatch.begin_down();
    }
    if (inventory_click.window == window && inventory_click.generation == generation && injected &&
        message == WM_LBUTTONUP) {
        inventory_click.dispatch.begin_up();
    }
}

void finish_inventory_click(HWND window, UINT message, bool injected, std::uint64_t generation) {
    if (inventory_click.window != window || inventory_click.generation != generation ||
        (message != WM_LBUTTONDOWN && message != WM_LBUTTONDBLCLK && message != WM_LBUTTONUP)) {
        return;
    }
    if (!injected) {
        cancel_controller_inventory_click();
        return;
    }
    const bool complete = message == WM_LBUTTONUP ? inventory_click.dispatch.end_up()
                                                  : inventory_click.dispatch.end_down();
    if (!complete) {
        if (!inventory_click.dispatch.pending()) {
            cancel_controller_inventory_click();
        }
        return;
    }
    const auto click = inventory_click;
    inventory_click = {};
    POINT cursor{};
    if (controller_active && game_is_foreground(window) && GetCursorPos(&cursor) &&
        cursor.x == click.item_cursor.x && cursor.y == click.item_cursor.y) {
        if (click.scene_known) {
            clear_inventory_focus();
            move_controller_pointer(click.scene_cursor.x, click.scene_cursor.y);
        } else {
            leave_inventory(window);
        }
    } else {
        clear_inventory_focus();
    }
}

void show_settings(HWND window) {
    if (!dialog_open) {
        scrolling::reset();
        documents::release();
        playback::suspend_fast_forward_input();
        suspend_controller();
        dialog_open = true;
        update_settings_link(window, !IsIconic(window));
        show_settings_dialog(window);
        dialog_open = false;
        suspend_controller();
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR,
                             DWORD_PTR) {
    static bool settings_clicked = false;
    if (message == WM_PAINT) {
        // Native window repaints bypass the normal canvas-transfer hook.
        const native_game::CanvasPresentation presentation(native_game::canvas_dc());
        return DefSubclassProc(window, message, value, data);
    }
    if (message == WM_WINDOWPOSCHANGED) {
        const auto result = DefSubclassProc(window, message, value, data);
        position_settings_link(window);
        return result;
    }
    observe_mouse_button(message);
    const bool injected_left =
        (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP) &&
        static_cast<ULONG_PTR>(GetMessageExtraInfo()) == controller_event;
    if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) && !injected_left) {
        cancel_controller_inventory_click();
    }
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !value)) {
        reveal::suspend();
        scrolling::reset();
        playback::suspend_fast_forward_input();
        suspend_controller();
        navigation_keys.reset();
        f10_down = escape_consumed = right_click_consumed = settings_clicked = false;
    }
    if (message == WM_KEYDOWN && value == VK_ESCAPE) {
        cancel_dialogue_click();
        playback::suspend_fast_forward_input();
        stop_rumble();
        cancel_controller_inventory_click();
    }
    if (native_navigation_keys.owns(message, value, data) ||
        ((message == WM_KEYUP || message == WM_SYSKEYUP) &&
         navigation_keys.owns(message, value, data))) {
        return 0;
    }
    if (!dialog_open && !text_entry_busy() && documents::message(window, message, value, data)) {
        if (injected_left) {
            cancel_controller_inventory_click();
        }
        return 0;
    }
    if (!dialog_open && !text_entry_busy() && transcript::message(window, message, value, data)) {
        if (message == WM_KEYDOWN) {
            navigation_keys.consume(value);
        }
        if (message == WM_KEYUP) {
            navigation_keys.owns(message, value, data);
        }
        if (message == WM_RBUTTONDOWN) {
            right_click_consumed = true;
        }
        if (injected_left) {
            cancel_controller_inventory_click();
        }
        return 0;
    }
    if (!dialog_open && !text_entry_busy() && continue_message(window, message, value, data)) {
        return 0;
    }
    if (!dialog_open && !text_entry_busy() &&
        saves::browser_message(window, message, value, data)) {
        if (injected_left) {
            cancel_controller_inventory_click();
        }
        if (message == WM_KEYDOWN) {
            navigation_keys.consume(value);
        }
        return 0;
    }
    if (!dialog_open && !text_entry_busy() && quick_menu::message(window, message, value, data)) {
        if (message == WM_KEYDOWN) {
            navigation_keys.consume(value);
        }
        if (injected_left) {
            cancel_controller_inventory_click();
        }
        return 0;
    }
    if (!dialog_open && !text_entry_busy() &&
        (reveal::message(window, message, value, data) ||
         scrolling::message(window, message, value, data))) {
        return 0;
    }
    if (message == WM_RBUTTONDOWN && !dialog_open && !text_entry_busy()) {
        quick_menu::dismiss();
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
            if (injected_left) {
                cancel_controller_inventory_click();
            }
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
        if (injected_left) {
            cancel_controller_inventory_click();
        }
        return 0;
    }
    if (navigation_keys.owns(message, value, data)) {
        return 0;
    }
    if ((message == WM_CHAR || message == WM_KEYUP) && value == VK_ESCAPE && escape_consumed) {
        return 0;
    }
    if (message == WM_KEYDOWN && !dialog_open && !text_entry_busy()) {
        if (value == VK_F11 && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (!(data & (1L << 30))) {
                devtools::request_inspector();
            }
            navigation_keys.consume(value);
            return 0;
        }
        if (value == VK_F5 || value == VK_F9) {
            if (!(data & (1L << 30)) && game_is_foreground(window)) {
                quick_save(window, value == VK_F9);
            }
            navigation_keys.consume(value);
            return 0;
        }
        if (navigate_keyboard(window, value)) {
            native_navigation_keys.consume(value);
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
    if (message == WM_SYSCOMMAND && (value & 0xfff0) == inspector_command) {
        if (!dialog_open) {
            devtools::request_inspector();
        }
        return 0;
    }
    if (message == WM_SYSCOMMAND && (value & 0xfff0) == settings_command) {
        show_settings(window);
        return 0;
    }
    if (message == WM_TIMER && value == timer) {
        input::injected_input().recover();
        diagnostics::record_game_context();
        attach_modal_input(window);
        update_menu();
        update_quick_load();
        saves::update_browser(window);
        quick_menu::update(!dialog_open && !text_entry_busy() && game_is_foreground(window));
        transcript::update();
        documents::update(window, !dialog_open && !text_entry_busy() && game_is_foreground(window));
        scrolling::update(!dialog_open && !text_entry_busy() && game_is_foreground(window));
        update_autosave(window, dialog_open || text_entry_busy() || !game_is_foreground(window) ||
                                    transcript::active() || documents::active());
        update_notification(window);
        if (saves::browser_active() || transcript::active() || documents::active()) {
            reveal::update(window, false);
            update_inventory_focus(window, false);
            update_settings_link(window, false);
            update_highlight(window, false);
            update_controller_hints(window, false);
            devtools::update_inspector(window, false);
            poll_controller(window);
            return 0;
        }
        const bool focused = game_is_foreground(window);
        update_inventory_focus(window, focused && !dialog_open && !text_entry_busy());
        update_settings_link(window, !IsIconic(window));
        update_dialogue(focused && !dialog_open);
        update_login(window, focused && !dialog_open);
        update_highlight(window, focused && !dialog_open && !text_entry_busy());
        update_controller_hints(window, focused && !dialog_open && !text_entry_busy());
        update_text_entry(window, focused && !dialog_open);
        devtools::update_inspector(window, !dialog_open && !text_entry_busy());
        const bool pressed = focused && (GetAsyncKeyState(VK_F10) & 0x8000);
        const bool open = pressed && !f10_down;
        f10_down = pressed;
        if (!dialog_open) {
            if (open) {
                show_settings(window);
            } else {
                poll_controller(window);
            }
        } else {
            input::poll(false);
        }
        reveal::update(window, focused && !dialog_open && !text_entry_busy());
        return 0;
    }
    if (message == WM_NCDESTROY) {
        detach_controls();
    }
    const auto click_generation = inventory_click.generation;
    begin_dialogue_click(window, message);
    begin_inventory_mouse_dispatch(window, message, injected_left, click_generation);
    const auto result = DefSubclassProc(window, message, value, data);
    finish_dialogue_click(message);
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP) {
        finish_inventory_click(window, message, injected_left, click_generation);
    }
    return result;
}

}

bool game_is_foreground(HWND window) {
    // cnc-ddraw reports the game as foreground through GetForegroundWindow even when inactive.
    GUITHREADINFO thread{sizeof(GUITHREADINFO)};
    return GetGUIThreadInfo(0, &thread) && thread.hwndActive == window;
}

HWND playback_input_window() {
    return !dialog_open && !text_entry_busy() && !transcript::active() && !documents::active()
               ? game_window
               : nullptr;
}

void begin_controller_inventory_click(HWND window, POINT scene_cursor, POINT item_cursor) {
    inventory_click = {window,
                       scene_cursor,
                       item_cursor,
                       GetTickCount64(),
                       ++next_inventory_click_generation,
                       true,
                       {}};
    inventory_click.dispatch.queue();
}

void begin_controller_inventory_click(HWND window, POINT item_cursor) {
    inventory_click = {window, {}, item_cursor, GetTickCount64(), ++next_inventory_click_generation,
                       false,  {}};
    inventory_click.dispatch.queue();
}

void cancel_controller_inventory_click() {
    if (inventory_click.window) {
        clear_inventory_focus();
    }
    inventory_click = {};
}

bool controller_inventory_click_pending() {
    if (inventory_click.window && !inventory_click.dispatch.in_flight() &&
        GetTickCount64() - inventory_click.queued_at >= 1000) {
        cancel_controller_inventory_click();
    }
    return inventory_click.window != nullptr;
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
        attach_rumble(window);
        attach_menu();
        attach_dialogue(window);
        native_game::attach_native_render();
        attach_modal_input(window);
        timer = SetTimer(window, subclass_id, 16, nullptr);
        const auto menu = GetSystemMenu(window, FALSE);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, settings_command, L"Enhancements...\tF10");
        AppendMenuW(menu, MF_STRING, inspector_command, L"Developer tools...\tCtrl+F11");
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
    native_navigation_keys.reset();
    navigation_keys.reset();
    suspend_controller();
    detach_rumble();
    saves::release_browser();
    transcript::release();
    documents::release(true);
    scrolling::reset();
    reveal::release();
    devtools::release_inspector();
    detach_modal_input();
    release_settings_link();
    release_continue_menu();
    release_autosave();
    release_text_entry();
    release_highlight();
    release_controller_hints();
    release_notification();
    clear_inventory_focus();
    native_game::detach_native_render();
    detach_dialogue();
    quick_menu::release();
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
