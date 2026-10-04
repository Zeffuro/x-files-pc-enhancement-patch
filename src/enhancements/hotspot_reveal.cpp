#include "hotspot_reveal.h"
#include "reveal_model.h"
#include "controls.h"
#include "dialogue.h"
#include "inventory.h"
#include "scene_overlay.h"
#include "quick_save.h"
#include "ui/quick_menu.h"
#include "settings.h"
#include "game/render/native_render.h"
#include "game/render/canvas_surface.h"
#include <memory>

namespace enhancements::reveal {
namespace {
thread_local bool controller_held = false;
thread_local bool key_consumed = false;
thread_local Hold keyboard_hold, controller_hold;
thread_local std::vector<Marker> visible;
thread_local bool labels_enabled = true;
thread_local std::unique_ptr<native_game::CanvasSurface> surface;

bool available(HWND window) {
    return game_is_foreground(window) && !IsIconic(window) && !scene_overlay_active() &&
           !quick_menu::expanded() && !inventory_focused(window) && !current_dialogue() &&
           safe_save_available();
}

HDC paint(HDC background) {
    if (visible.empty() || !surface) {
        return background;
    }
    const auto dc = surface->copy(background);
    if (!dc) {
        return background;
    }
    draw(dc, visible, game::scene_bounds(), labels_enabled);
    return dc;
}

void show(HWND window, std::vector<Marker> next, bool labels = true) {
    bool same = next.size() == visible.size() && labels == labels_enabled;
    for (std::size_t i = 0; same && i < next.size(); ++i) {
        same = EqualRect(&next[i].bounds, &visible[i].bounds) &&
               next[i].direction == visible[i].direction &&
               next[i].interaction == visible[i].interaction;
    }
    if (same) {
        return;
    }
    if (!next.empty() && !surface) {
        surface = std::make_unique<native_game::CanvasSurface>();
    }
    visible = std::move(next);
    labels_enabled = labels;
    native_game::set_canvas_reveal(visible.empty() ? nullptr : paint);
    native_game::invalidate_canvas();
    if (IsWindow(window)) {
        InvalidateRect(window, nullptr, FALSE);
    }
}
}

void controller(bool held) {
    controller_held = held;
}

bool message(HWND window, UINT event, WPARAM value, LPARAM data) {
    const auto key = settings().hotspot_reveal_key;
    const bool alt = key == VK_LMENU && value == VK_MENU && !(data & (1L << 24));
    const bool matches = key && (value == key || alt);
    if ((event == WM_SYSKEYDOWN || event == WM_KEYDOWN) &&
        (value == VK_RETURN || value == VK_TAB) && (GetAsyncKeyState(VK_LMENU) & 0x8000)) {
        suspend();
    }
    if ((event == WM_KEYDOWN || event == WM_SYSKEYDOWN) && matches && settings().hotspot_reveal &&
        available(window)) {
        key_consumed = true;
        update(window, true);
        return true;
    }
    if ((event == WM_KEYUP || event == WM_SYSKEYUP) && matches && key_consumed) {
        key_consumed = false;
        update(window, true);
        return true;
    }
    return event == WM_CHAR && key_consumed && (value == key || value == key + ('a' - 'A'));
}

void update(HWND window, bool enabled) {
    const auto& value = settings();
    const bool keyboard =
        value.hotspot_reveal_key && (GetAsyncKeyState(value.hotspot_reveal_key) & 0x8000);
    const bool allowed = enabled && value.hotspot_reveal && game_is_foreground(window) &&
                         !IsIconic(window) && !scene_overlay_active() && !quick_menu::expanded() &&
                         !inventory_focused(window) && !current_dialogue();
    const bool ready = allowed && (keyboard || controller_held) && safe_save_available();
    bool barrier = !allowed;
    if (allowed && !ready && (keyboard || controller_held)) {
        const auto input = game::input_vtable();
        const auto script = game::script_controls();
        barrier =
            (input && input != game::edition().movie && input != game::edition().action_movie) ||
            script.script_dialog || script.text_input || !script.buttons.empty() ||
            !game::emotion_targets().empty();
    }
    if (barrier) {
        keyboard_hold.suspend();
        controller_hold.suspend();
    }
    const bool shortcut =
        (GetAsyncKeyState(VK_RMENU) & 0x8000) || (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
        (GetAsyncKeyState(VK_RETURN) & 0x8000) || (GetAsyncKeyState(VK_TAB) & 0x8000);
    if (keyboard && shortcut) {
        keyboard_hold.suspend();
    }
    const bool keyboard_visible = keyboard_hold.update(keyboard, ready && !shortcut);
    const bool controller_visible = controller_hold.update(controller_held, ready);
    auto targets = keyboard_visible || controller_visible ? game::world_targets()
                                                          : std::vector<game::WorldTarget>{};
    show(window,
         keyboard_visible || controller_visible
             ? markers(targets, game::scene_bounds(), value.hotspot_exits_only)
             : std::vector<Marker>{},
         value.hotspot_labels);
}

void suspend() {
    controller_held = false;
    keyboard_hold.suspend();
    controller_hold.suspend();
    show(nullptr, {});
}

void release() {
    suspend();
    surface.reset();
}
}
