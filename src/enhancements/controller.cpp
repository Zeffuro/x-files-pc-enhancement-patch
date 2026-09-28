#include "input_source.h"
#include "controller_state.h"
#include "gun.h"
#include "controller_navigation.h"
#include "controller_context.h"
#include "rumble.h"
#include "controls.h"
#include "dialogue.h"
#include "inventory.h"
#include "screens.h"
#include "settings.h"
#include "text_entry.h"
#include "focus.h"
#include "game_ui.h"
#include "spring_cursor.h"
#include "saves/browser.h"

#include <algorithm>

namespace enhancements {
namespace {

thread_local SpringCursor spring_cursor;
thread_local input::MotionAccumulator analog_motion;
thread_local input::MotionAccumulator motion;
thread_local input::NavigationRepeat navigation;
thread_local input::ContextBarrier context_barrier;
thread_local std::uint64_t last = 0;

void reset_navigation() {
    spring_cursor.suspend();
    analog_motion.reset();
    motion.reset();
    navigation.reset();
}

bool scene_cursor(HWND window, POINT& point) {
    const auto scene = game::scene_bounds();
    if (IsRectEmpty(&scene) || !GetCursorPos(&point) || !ScreenToClient(window, &point)) {
        return false;
    }
    if (!PtInRect(&scene, point)) {
        point = {(scene.left + scene.right) / 2, (scene.top + scene.bottom) / 2};
    }
    return ClientToScreen(window, &point) != FALSE;
}

input::Context controller_context(HWND window) {
    input::Context context;
    context.native = reinterpret_cast<std::uintptr_t>(game::current_input());
    if (saves::browser_active()) {
        context.kind = input::ContextKind::browser;
    } else if (game::menu_confirmation_active()) {
        context.kind = input::ContextKind::modal;
    } else if (game::input_vtable() == game::edition().main_menu) {
        context.kind = input::ContextKind::menu;
    } else if (inventory_focused(window)) {
        context.kind = input::ContextKind::inventory;
    } else if (const auto dialogue = current_dialogue()) {
        context.kind = input::ContextKind::dialogue;
        context.history = dialogue->is_history;
    } else if (context.native) {
        context.kind = input::ContextKind::native;
    } else {
        auto script = game::script_controls();
        if (!script.buttons.empty() || script.script_dialog || !script.dialog_buttons.empty() ||
            !script.acknowledgement_buttons.empty()) {
            context.kind = script.script_dialog || !script.acknowledgement_buttons.empty()
                               ? input::ContextKind::script_dialog
                               : input::ContextKind::script;
            context.resources = std::move(script.resources);
            std::sort(context.resources.begin(), context.resources.end());
            context.resources.erase(std::unique(context.resources.begin(), context.resources.end()),
                                    context.resources.end());
        } else if (!game::emotion_targets().empty()) {
            context.kind = input::ContextKind::emotions;
        }
    }
    return context;
}

}

void suspend_analog_cursor() {
    spring_cursor.suspend();
    analog_motion.reset();
}

void suspend_controller() {
    input::poll(false);
    input::injected_input().recover();
    cancel_controller_inventory_click();
    clear_inventory_focus();
    stop_rumble();
    reset_navigation();
    context_barrier.reset();
    controller_active = false;
    last = 0;
}

void move_analog_cursor(HWND window, SHORT horizontal, SHORT vertical, float elapsed) {
    if (settings().spring_cursor) {
        RECT bounds{};
        POINT position{};
        analog_motion.reset();
        if (GetClientRect(window, &bounds) &&
            spring_cursor.update(horizontal, vertical, bounds, position) &&
            ClientToScreen(window, &position)) {
            move_controller_pointer(position.x, position.y);
        }
        return;
    }
    spring_cursor.suspend();
    const auto movement = analog_motion.advance(input::cursor_axis(horizontal) * 450,
                                                -input::cursor_axis(vertical) * 450, elapsed);
    const auto dx = movement.x, dy = movement.y;
    RECT client{};
    POINT cursor{};
    if ((dx || dy) && GetClientRect(window, &client) && client.right > 0 && client.bottom > 0 &&
        GetCursorPos(&cursor) && ScreenToClient(window, &cursor)) {
        cursor.x = std::clamp(cursor.x + dx, client.left, client.right - 1);
        cursor.y = std::clamp(cursor.y + dy, client.top, client.bottom - 1);
        if (ClientToScreen(window, &cursor)) {
            move_controller_pointer(cursor.x, cursor.y);
        }
    }
}

void poll_controller(HWND window) {
    if (controller_gun_busy()) {
        return;
    }
    const auto now = GetTickCount64();
    const auto elapsed = input::capped_elapsed(now, last);
    const bool delivery_ready = input::injected_input().recover();
    observe_pointer();
    const bool focused = game_is_foreground(window);
    const bool busy = text_entry_busy();
    auto frame = busy ? input::Frame{} : input::poll(settings().gamepad && focused);
    if (frame.device_changed) {
        cancel_controller_inventory_click();
        stop_rumble();
        controller_active = false;
        reset_navigation();
        context_barrier.reset();
    }
    if (!frame.connected || !settings().gamepad || !focused || busy) {
        cancel_controller_inventory_click();
        stop_rumble();
        if (!busy || !focused) {
            spring_cursor.suspend();
        }
        analog_motion.reset();
        if ((!frame.connected && !busy) || !focused || !settings().gamepad) {
            controller_active = false;
        }
        motion.reset();
        navigation.reset();
        context_barrier.reset();
        return;
    }
    if (!delivery_ready) {
        input::poll(false);
        stop_rumble();
        reset_navigation();
        return;
    }
    const bool pending_click = controller_inventory_click_pending();
    if (!pending_click && context_barrier.filter(controller_context(window), frame)) {
        reset_navigation();
    }
    const auto& state = frame.sample;
    const auto buttons = state.buttons;
    const bool aim = state.left_trigger > input::trigger_threshold;
    const bool aim_pressed = frame.aim_pressed;
    if (buttons || aim || state.right_trigger > input::trigger_threshold ||
        input::cursor_axis(state.left_x) || input::cursor_axis(state.left_y)) {
        controller_active = true;
    }
    update_rumble(frame.player, settings().vibration && controller_active &&
                                    !saves::browser_active() && !game::menu_confirmation_active() &&
                                    game::input_vtable() != game::edition().main_menu);
    const WORD pressed = frame.pressed;
    if (saves::browser_active()) {
        cancel_controller_inventory_click();
        spring_cursor.suspend();
        analog_motion.reset();
        motion.reset();
        navigation.reset();
        const WORD key =
            (pressed & (input::button::inventory | input::button::back | input::button::menu))
                ? VK_ESCAPE
            : (pressed & input::button::previous) ? VK_PRIOR
            : (pressed & input::button::next)     ? VK_NEXT
            : (pressed & input::button::left)     ? VK_LEFT
            : (pressed & input::button::right)    ? VK_RIGHT
            : (pressed & input::button::up)       ? VK_UP
            : (pressed & input::button::down)     ? VK_DOWN
            : (pressed & input::button::examine)  ? VK_TAB
            : (pressed & input::button::activate) ? VK_RETURN
                                                  : 0;
        if (key) {
            saves::browser_message(window, WM_KEYDOWN, key, 0);
        }
        return;
    }
    if (pressed & input::button::menu) {
        suspend_controller();
        input::injected_input().key(VK_ESCAPE, 0, controller_event);
        return;
    }
    if (pressed & input::button::skip) {
        stop_rumble();
        analog_motion.reset();
        motion.reset();
        navigation.reset();
        if (game::movie_skippable()) {
            input::injected_input().key(VK_SPACE, 0, controller_event);
        }
        return;
    }
    if (pending_click) {
        return;
    }
    const bool all_hotspots = state.right_trigger > input::trigger_threshold;
    const auto aiming = aim && !current_dialogue() && !inventory_focused(window)
                            ? game::aiming_targets()
                            : std::vector<RECT>{};
    const bool aim_mode = !aiming.empty();
    const bool jump_mode =
        aim_mode ||
        ((all_hotspots || (buttons & (input::button::previous | input::button::next))) &&
         !current_dialogue() && !inventory_focused(window) && game::world_navigation_available());
    const bool analog_cursor = settings().analog_cursor;
    constexpr WORD focus_buttons = input::button::up | input::button::down | input::button::left |
                                   input::button::right | input::button::previous |
                                   input::button::next | input::button::inventory |
                                   input::button::back | input::button::evidence;
    if ((jump_mode && !aim_mode) || !analog_cursor || (buttons & focus_buttons)) {
        spring_cursor.suspend();
    }
    if (analog_cursor && (!jump_mode || aim_mode) &&
        !(settings().spring_cursor && (buttons & focus_buttons))) {
        move_analog_cursor(window, state.left_x, state.left_y, elapsed);
    }
    if (pressed & input::button::examine) {
        input::injected_input().click(true, controller_event);
        return;
    }
    if ((pressed & input::button::evidence) && focus_conversation_evidence(window)) {
        return;
    }
    const auto nav = navigation.update(state, analog_cursor, jump_mode, aim_mode, now);
    const int horizontal_direction = nav.horizontal;
    const int vertical_direction = nav.vertical;
    const bool step = nav.step;
    if (navigate_screen(window, step ? horizontal_direction : 0, step ? vertical_direction : 0,
                        (pressed & input::button::activate) != 0,
                        (pressed & input::button::back) != 0,
                        (pressed & input::button::inventory) != 0)) {
        motion.reset();
        return;
    }
    POINT return_cursor{}, original_cursor{};
    if (aim_pressed && !current_dialogue() && (game::world_navigation_available() || aim_mode) &&
        GetCursorPos(&original_cursor) && scene_cursor(window, return_cursor)) {
        const auto* view = game::current_view();
        spring_cursor.suspend();
        if (equip_controller_gun()) {
            clear_inventory_focus();
            POINT current{};
            if (game_is_foreground(window) && game::current_view() == view &&
                GetCursorPos(&current) && current.x == original_cursor.x &&
                current.y == original_cursor.y) {
                const bool stationary =
                    return_cursor.x == current.x && return_cursor.y == current.y;
                if ((stationary || move_controller_pointer(return_cursor.x, return_cursor.y)) &&
                    game_is_foreground(window) && game::current_view() == view &&
                    GetCursorPos(&current) && current.x == return_cursor.x &&
                    current.y == return_cursor.y && ScreenToClient(window, &current)) {
                    refresh_controller_gun_cursor(current);
                }
            }
        }
        return;
    }
    if (pressed & input::button::inventory) {
        if (navigate_emotions(window, 0, true)) {
            return;
        }
        if (!close_dialogue(window)) {
            focus_inventory(window);
        }
        return;
    }
    const bool inventory = inventory_focused(window);
    if (step &&
        navigate_emotions(window, horizontal_direction ? horizontal_direction : vertical_direction,
                          false)) {
        motion.reset();
        return;
    }
    const bool dialogue = !inventory && current_dialogue() != nullptr;
    constexpr WORD navigation_buttons = input::button::up | input::button::down |
                                        input::button::left | input::button::right |
                                        input::button::previous | input::button::next;
    const bool selecting = (dialogue || inventory) && (buttons & navigation_buttons);
    if (inventory) {
        const int direction =
            (pressed & input::button::right ? 1 : 0) - (pressed & input::button::left ? 1 : 0);
        navigate_inventory(window, direction);
    }
    if (dialogue) {
        const int direction =
            (pressed & input::button::down ? 1 : 0) - (pressed & input::button::up ? 1 : 0);
        const int tab = (pressed & (input::button::next | input::button::right) ? 1 : 0) -
                        (pressed & (input::button::previous | input::button::left) ? 1 : 0);
        navigate_dialogue(window, direction, tab);
    }
    constexpr float speed = 450;
    constexpr float fine_speed = 90;
    const auto directional_buttons = dialogue || inventory ? WORD{0} : buttons;
    const auto horizontal = (directional_buttons & input::button::right ? 1 : 0) -
                            (directional_buttons & input::button::left ? 1 : 0);
    const auto vertical = (directional_buttons & input::button::down ? 1 : 0) -
                          (directional_buttons & input::button::up ? 1 : 0);
    input::Motion movement{};
    if (jump_mode) {
        if (step && horizontal_direction) {
            const auto targets = aim_mode ? aiming : game::world_hotspots(!all_hotspots);
            POINT cursor{};
            if (GetCursorPos(&cursor) && ScreenToClient(window, &cursor)) {
                const auto next = hotspot_target(targets, cursor, horizontal_direction);
                if (next >= 0) {
                    point_controller(window, targets[next]);
                }
            }
        }
        motion.reset();
    } else if (selecting) {
        motion.reset();
    } else {
        movement = motion.advance((analog_cursor ? 0 : input::cursor_axis(state.left_x)) * speed +
                                      horizontal * fine_speed,
                                  -(analog_cursor ? 0 : input::cursor_axis(state.left_y)) * speed +
                                      vertical * fine_speed,
                                  elapsed);
    }
    const bool clicking = (pressed & (input::button::activate | input::button::examine)) != 0;
    if (movement.x || movement.y || clicking) {
        RECT client{};
        POINT cursor{};
        if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0 ||
            !GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) {
            return;
        }
        cursor.x = std::clamp(cursor.x + movement.x, client.left, client.right - 1);
        cursor.y = std::clamp(cursor.y + movement.y, client.top, client.bottom - 1);
        if (!ClientToScreen(window, &cursor) || !move_controller_pointer(cursor.x, cursor.y)) {
            return;
        }
    }
    if (pressed & input::button::activate) {
        if (inventory) {
            POINT item_cursor{};
            if (GetCursorPos(&item_cursor)) {
                begin_controller_inventory_click(window, item_cursor);
            }
        }
        const auto result = input::injected_input().click(false, controller_event);
        if (inventory && !result.started()) {
            leave_inventory(window);
            cancel_controller_inventory_click();
        }
    }
    if (pressed & input::button::back) {
        if (!leave_inventory(window)) {
            close_dialogue(window);
        }
    }
}

}
