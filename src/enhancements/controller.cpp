#include "input_source.h"
#include "controller_state.h"
#include "enhancements/game_resources.h"
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
#include <cmath>

namespace enhancements {
namespace {

thread_local SpringCursor spring_cursor;

float axis(SHORT value) {
    const int magnitude = std::abs(static_cast<int>(value));
    constexpr int deadzone = input::left_deadzone;
    if (magnitude <= deadzone) {
        return 0;
    }
    const auto normalized = std::min(1.0f, float(magnitude - deadzone) / (32767 - deadzone));
    return std::copysign(normalized * normalized, static_cast<float>(value));
}

void click(DWORD down, DWORD up) {
    INPUT events[2]{};
    events[0].type = events[1].type = INPUT_MOUSE;
    events[0].mi.dwExtraInfo = events[1].mi.dwExtraInfo = controller_event;
    events[0].mi.dwFlags = down;
    events[1].mi.dwFlags = up;
    SendInput(2, events, sizeof(INPUT));
}

void press_key(WORD key) {
    INPUT events[2]{};
    events[0].type = events[1].type = INPUT_KEYBOARD;
    events[0].ki.wVk = events[1].ki.wVk = key;
    events[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, events, sizeof(INPUT));
}

}

void suspend_analog_cursor() {
    spring_cursor.suspend();
}

void move_analog_cursor(HWND window, SHORT horizontal, SHORT vertical, float elapsed) {
    static float remainder_x = 0, remainder_y = 0;
    if (settings().spring_cursor) {
        RECT bounds{};
        POINT position{};
        remainder_x = remainder_y = 0;
        if (GetClientRect(window, &bounds) &&
            spring_cursor.update(horizontal, vertical, bounds, position) &&
            ClientToScreen(window, &position)) {
            move_controller_pointer(position.x, position.y);
        }
        return;
    }
    spring_cursor.suspend();
    remainder_x += axis(horizontal) * 450 * elapsed;
    remainder_y -= axis(vertical) * 450 * elapsed;
    const auto dx = static_cast<LONG>(remainder_x), dy = static_cast<LONG>(remainder_y);
    remainder_x -= dx;
    remainder_y -= dy;
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
    static thread_local ULONGLONG last = 0;
    static thread_local float remainder_x = 0;
    static thread_local float remainder_y = 0;
    static thread_local bool return_to_scene = false;
    static thread_local int previous_horizontal = 0, previous_vertical = 0;
    static thread_local ULONGLONG repeat_at = 0;
    static thread_local void* previous_input = nullptr;
    const auto now = GetTickCount64();
    const auto elapsed = last ? std::min(0.05f, (now - last) / 1000.0f) : 0.0f;
    last = now;
    observe_pointer();
    const bool focused = game_is_foreground(window);
    const bool busy = text_entry_busy();
    const auto frame = busy ? input::Frame{} : input::poll(settings().gamepad && focused);
    if (frame.device_changed) {
        spring_cursor.suspend();
        previous_input = nullptr;
        remainder_x = remainder_y = 0;
        return_to_scene = false;
        previous_horizontal = previous_vertical = 0;
        repeat_at = 0;
    }
    if (!frame.connected || !settings().gamepad || !focused || busy) {
        if (!busy || !focused) {
            spring_cursor.suspend();
        }
        if ((!frame.connected && !busy) || !focused || !settings().gamepad) {
            controller_active = false;
        }
        remainder_x = remainder_y = 0;
        return_to_scene = false;
        previous_horizontal = previous_vertical = 0;
        repeat_at = 0;
        return;
    }
    const auto& state = frame.sample;
    const auto buttons = state.buttons;
    const bool aim = state.left_trigger > input::trigger_threshold;
    const bool aim_pressed = frame.aim_pressed;
    if (buttons || aim || state.right_trigger > input::trigger_threshold || axis(state.left_x) ||
        axis(state.left_y)) {
        controller_active = true;
    }
    const WORD pressed = frame.pressed;
    if (saves::browser_active()) {
        spring_cursor.suspend();
        return_to_scene = false;
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
    if (return_to_scene) {
        // Let the queued inventory click finish before restoring the aim position.
        leave_inventory(window);
        return_to_scene = false;
        return;
    }
    if (pressed & input::button::menu) {
        spring_cursor.suspend();
        press_key(VK_ESCAPE);
        return;
    }
    if (pressed & input::button::skip) {
        if (game::movie_skippable()) {
            press_key(VK_SPACE);
        }
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
    const auto input = game::current_input();
    if (input != previous_input) {
        spring_cursor.suspend();
        previous_input = input;
    }
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
        click(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
        return;
    }
    if ((pressed & input::button::evidence) && focus_conversation_evidence(window)) {
        return;
    }
    const auto horizontal_input =
        (buttons & input::button::right ? 1 : 0) - (buttons & input::button::left ? 1 : 0) +
        ((!analog_cursor || (jump_mode && !aim_mode)) && state.left_x > 18000 ? 1 : 0) -
        ((!analog_cursor || (jump_mode && !aim_mode)) && state.left_x < -18000 ? 1 : 0);
    const auto vertical_input = (buttons & input::button::down ? 1 : 0) -
                                (buttons & input::button::up ? 1 : 0) +
                                (!analog_cursor && state.left_y < -18000 ? 1 : 0) -
                                (!analog_cursor && state.left_y > 18000 ? 1 : 0);
    const int horizontal_direction = std::clamp(horizontal_input, -1, 1);
    const int vertical_direction = std::clamp(vertical_input, -1, 1);
    const bool changed =
        horizontal_direction != previous_horizontal || vertical_direction != previous_vertical;
    const bool step = changed || now >= repeat_at;
    if (step) {
        repeat_at = now + (changed ? 350 : 140);
    }
    previous_horizontal = horizontal_direction;
    previous_vertical = vertical_direction;
    if (navigate_screen(window, step ? horizontal_direction : 0, step ? vertical_direction : 0,
                        (pressed & input::button::activate) != 0,
                        (pressed & input::button::back) != 0,
                        (pressed & input::button::inventory) != 0)) {
        remainder_x = remainder_y = 0;
        return_to_scene = false;
        return;
    }
    if (aim_pressed && !current_dialogue() && (game::world_navigation_available() || aim_mode) &&
        focus_inventory_item(window, resource::inventory_gun)) {
        spring_cursor.suspend();
        click(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
        return_to_scene = true;
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
        remainder_x = remainder_y = 0;
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
        remainder_x = remainder_y = 0;
    } else if (selecting) {
        remainder_x = remainder_y = 0;
    } else {
        remainder_x +=
            ((analog_cursor ? 0 : axis(state.left_x)) * speed + horizontal * fine_speed) * elapsed;
        remainder_y +=
            (-(analog_cursor ? 0 : axis(state.left_y)) * speed + vertical * fine_speed) * elapsed;
    }
    const auto dx = static_cast<LONG>(remainder_x);
    const auto dy = static_cast<LONG>(remainder_y);
    remainder_x -= dx;
    remainder_y -= dy;
    const bool clicking = (pressed & (input::button::activate | input::button::examine)) != 0;
    if (dx || dy || clicking) {
        RECT client{};
        POINT cursor{};
        if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0 ||
            !GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) {
            return;
        }
        cursor.x = std::clamp(cursor.x + dx, client.left, client.right - 1);
        cursor.y = std::clamp(cursor.y + dy, client.top, client.bottom - 1);
        if (!ClientToScreen(window, &cursor) || !move_controller_pointer(cursor.x, cursor.y)) {
            return;
        }
    }
    if (pressed & input::button::activate) {
        click(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
        return_to_scene = inventory;
    }
    if (pressed & input::button::back) {
        if (!leave_inventory(window)) {
            close_dialogue(window);
        }
    }
}

}
