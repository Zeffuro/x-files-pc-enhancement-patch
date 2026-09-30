#include "quick_menu.h"
#include "quick_menu_surface.h"
#include "enhancements/game_ui.h"
#include "enhancements/menu_corner.h"
#include "enhancements/gun.h"
#include "enhancements/quick_save.h"
#include "enhancements/scene_overlay.h"
#include "enhancements/dialogue.h"
#include "enhancements/controls.h"
#include "enhancements/input_source.h"
#include "enhancements/focus.h"
#include "game/render/native_render.h"
#include "saves/browser.h"
#include "transcript/view.h"
#include "settings.h"
#include <algorithm>
#include <memory>

namespace enhancements::quick_menu {
namespace {
std::unique_ptr<Surface> surface;
std::optional<RECT> native_button;
std::optional<game::MenuCornerIdentity> native_owner;
bool opened = false, consumed = false, bypass = false;
bool suppressed = false, hover_pending = false;
bool corner_hovered = false;
HWND menu_requested = nullptr;
HWND pointer_window = nullptr;
std::optional<game::MenuCornerIdentity> menu_owner;
RECT menu_original{};
int hover = -1, pressed = -1, progress = 0;
ULONGLONG previous_tick = 0;
ULONGLONG bypass_until = 0;
std::array<bool, 5> enabled{};
std::array<bool, 5> visible{true, true, true, true, true};
bool menu_configured = true;

bool configured() {
    return settings().quick_menu &&
           std::any_of(settings().quick_menu_items.begin(), settings().quick_menu_items.end(),
                       [](bool item) { return item; });
}

bool available() {
    return checkpoint_available() || (current_dialogue() && scene_overlay_available(false));
}

int hit(POINT point) {
    if (!native_button) {
        return -1;
    }
    auto bounds = Surface::toggle(*native_button);
    if (PtInRect(&bounds, point)) {
        return 5;
    }
    if (opened && progress == 100) {
        for (unsigned i = 0; i < enabled.size(); ++i) {
            if (!visible[i]) {
                continue;
            }
            bounds = Surface::button(*native_button, i, visible);
            if (PtInRect(&bounds, point)) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

void pointer(POINT point) {
    const auto item = hit(point);
    auto reveal = native_button ? Surface::reveal(*native_button) : RECT{};
    const bool corner = native_button && PtInRect(&reveal, point);
    if (hover != item || corner_hovered != corner) {
        hover = item;
        corner_hovered = corner;
        native_game::invalidate_canvas();
    }
}

void activate(HWND window, int item) {
    if (item == 5) {
        opened = !opened;
        native_game::invalidate_canvas();
        return;
    }
    if (item < 0 || !enabled[item]) {
        return;
    }
    const auto original = *native_button;
    dismiss();
    if (item == 2) {
        transcript::show();
    } else if (item < 2) {
        saves::show_browser(item == 0);
    } else if (item == 3) {
        request_settings(window);
    } else {
        menu_owner = game::menu_corner_identity();
        if (menu_owner) {
            menu_requested = window;
            menu_original = original;
        }
    }
}
}

void dismiss() {
    opened = false;
    corner_hovered = false;
    pressed = hover = -1;
    native_game::invalidate_canvas();
}

bool expanded() {
    return opened && native_button.has_value();
}

void update(bool focused) {
    const auto active_config = configured();
    if (visible != settings().quick_menu_items || menu_configured != active_config) {
        menu_configured = active_config;
        visible = settings().quick_menu_items;
        dismiss();
        progress = 0;
        menu_requested = nullptr;
        menu_owner.reset();
    }
    if (bypass && GetTickCount64() > bypass_until) {
        bypass = false;
    }
    if (menu_requested && (!focused || !game_is_foreground(menu_requested))) {
        menu_requested = nullptr;
        menu_owner.reset();
    }
    if (menu_requested && !input::injected_input().pending()) {
        const auto current = game::menu_corner();
        POINT point{(menu_original.left + menu_original.right) / 2,
                    (menu_original.top + menu_original.bottom) / 2};
        if (current && EqualRect(&*current, &menu_original) &&
            game::menu_corner_identity() == menu_owner && available() &&
            ClientToScreen(menu_requested, &point) && game::suppress_menu_corner(false)) {
            suppressed = false;
            bypass = true;
            bypass_until = GetTickCount64() + 500;
            if (!move_controller_pointer(point.x, point.y) ||
                !input::injected_input().click(false, controller_event).started()) {
                bypass = false;
            }
        }
        menu_requested = nullptr;
        menu_owner.reset();
    }
    std::optional<RECT> next;
    std::optional<game::MenuCornerIdentity> next_owner;
    if (configured() && native_game::native_render_available() && available() &&
        game::input_vtable() != game::edition().main_menu) {
        next = game::menu_corner();
        next_owner = next ? game::menu_corner_identity() : std::nullopt;
        if (!next_owner) {
            next.reset();
        }
    }
    const bool want = next.has_value() && !bypass;
    if (!focused || transcript::active() || saves::browser_active()) {
        next.reset();
        next_owner.reset();
    }
    if (next_owner != native_owner || next.has_value() != native_button.has_value() ||
        (next && native_button && !EqualRect(&*next, &*native_button))) {
        dismiss();
        progress = 0;
        native_game::invalidate_canvas();
    }
    native_button = next;
    native_owner = next_owner;
    const bool restored_or_suppressed = game::suppress_menu_corner(want);
    const bool own = want && restored_or_suppressed;
    if (own && !suppressed) {
        hover_pending = true;
    }
    suppressed = own;
    if (next && !own && !bypass) {
        native_button.reset();
        dismiss();
        return;
    }
    if (!own) {
        hover_pending = false;
    } else if (focused && hover_pending) {
        POINT cursor{};
        if (GetCursorPos(&cursor) && refresh_native_cursor(cursor)) {
            hover_pending = false;
        }
    }
    if (!next) {
        return;
    }
    if (!surface) {
        try {
            surface = std::make_unique<Surface>();
        } catch (...) {
            game::suppress_menu_corner(false);
            suppressed = hover_pending = false;
            native_button.reset();
            return;
        }
    }
    enabled = {settings().save_browser && export_save_available(),
               settings().save_browser && checkpoint_available(), settings().dialogue_transcript,
               true, true};
    for (unsigned i = 0; i < enabled.size(); ++i) {
        enabled[i] = enabled[i] && visible[i];
    }
    POINT cursor{};
    if (pointer_window && IsWindow(pointer_window) && GetCursorPos(&cursor) &&
        ScreenToClient(pointer_window, &cursor)) {
        pointer(cursor);
    }
    const auto now = GetTickCount64();
    const auto step = static_cast<int>(std::min<ULONGLONG>(32, now - previous_tick));
    previous_tick = now;
    const auto changed = std::clamp(progress + (opened ? step : -step), 0, 100);
    if (changed != progress) {
        progress = changed;
        native_game::invalidate_canvas();
    }
}

HDC canvas(HDC background) {
    return native_button && surface && (corner_hovered || opened || progress)
               ? surface->draw(background, *native_button, progress, hover, enabled, visible)
               : background;
}

std::vector<RECT> targets() {
    std::vector<RECT> result;
    if (native_button) {
        if (opened) {
            for (unsigned i = 0; i < enabled.size(); ++i) {
                if (enabled[i]) {
                    result.push_back(Surface::button(*native_button, i, visible));
                }
            }
        }
        result.push_back(Surface::toggle(*native_button));
    }
    return result;
}

bool message(HWND window, UINT id, WPARAM value, LPARAM) {
    if (id == WM_KILLFOCUS || (id == WM_ACTIVATEAPP && !value)) {
        menu_requested = nullptr;
        menu_owner.reset();
        bypass = false;
        if (configured() && available() && game::input_vtable() != game::edition().main_menu) {
            suppressed = game::suppress_menu_corner(true);
        }
        dismiss();
        native_button.reset();
        native_owner.reset();
        progress = 0;
        hover_pending = suppressed;
        return false;
    }
    if (bypass && id == WM_MOUSEMOVE) {
        return false;
    }
    if (bypass && static_cast<ULONG_PTR>(GetMessageExtraInfo()) == controller_event &&
        (id == WM_LBUTTONDOWN || id == WM_LBUTTONUP)) {
        // The native click remains queued after its window messages have been delivered.
        return false;
    }
    if (id == WM_LBUTTONDOWN || id == WM_LBUTTONDBLCLK) {
        consumed = false;
        pressed = -1;
    }
    if (id == WM_LBUTTONUP && consumed && !native_button) {
        consumed = false;
        return true;
    }
    if (!native_button) {
        return false;
    }
    if (opened && id == WM_KEYDOWN && value == VK_ESCAPE) {
        dismiss();
        return true;
    }
    if (id == WM_MOUSEMOVE || id == WM_LBUTTONDOWN || id == WM_LBUTTONDBLCLK ||
        id == WM_LBUTTONUP) {
        pointer_window = window;
        POINT point{};
        if (!GetCursorPos(&point) || !ScreenToClient(window, &point)) {
            return false;
        }
        const auto item = hit(point);
        pointer(point);
        const auto region = opened || progress ? Surface::region(*native_button, visible)
                                               : Surface::reveal(*native_button);
        const bool inside = PtInRect(&region, point) != FALSE;
        if (id == WM_MOUSEMOVE) {
            // cnc-ddraw must update its scaled cursor before the next button message.
            return false;
        }
        if (id != WM_LBUTTONUP) {
            if (inside) {
                consumed = true;
                pressed = item;
                return true;
            }
            dismiss();
        } else if (consumed) {
            consumed = false;
            const auto selected = pressed;
            pressed = -1;
            if (selected == item && item >= 0 && game::menu_corner_identity() == native_owner) {
                activate(window, item);
            }
            return true;
        }
    }
    return false;
}

void release() {
    game::suppress_menu_corner(false);
    suppressed = hover_pending = false;
    dismiss();
    consumed = bypass = false;
    menu_requested = nullptr;
    pointer_window = nullptr;
    menu_owner.reset();
    native_button.reset();
    native_owner.reset();
    surface.reset();
    progress = 0;
}
}
