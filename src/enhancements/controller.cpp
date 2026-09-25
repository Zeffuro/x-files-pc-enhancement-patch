#include "input_source.h"
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

#include <xinput.h>
#include <algorithm>
#include <cmath>

namespace enhancements {
namespace {

thread_local SpringCursor spring_cursor;

float axis(SHORT value) {
    const int magnitude = std::abs(static_cast<int>(value));
    constexpr int deadzone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
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
    static thread_local WORD previous = 0;
    static thread_local ULONGLONG last = 0;
    static thread_local float remainder_x = 0;
    static thread_local float remainder_y = 0;
    static thread_local bool return_to_scene = false;
    static thread_local int previous_horizontal = 0, previous_vertical = 0;
    static thread_local ULONGLONG repeat_at = 0;
    static thread_local void* previous_input = nullptr;
    static thread_local bool previous_aim = false;
    const auto now = GetTickCount64();
    const auto elapsed = last ? std::min(0.05f, (now - last) / 1000.0f) : 0.0f;
    last = now;
    observe_pointer();
    XINPUT_STATE state{};
    bool connected = false;
    for (DWORD player = 0; player < XUSER_MAX_COUNT; ++player) {
        if (XInputGetState(player, &state) == ERROR_SUCCESS) {
            connected = true;
            break;
        }
    }
    const auto buttons = state.Gamepad.wButtons;
    const bool aim = state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    const bool aim_pressed = aim && !previous_aim;
    previous_aim = aim;
    if (!connected || !settings().gamepad || !game_is_foreground(window) || text_entry_busy()) {
        if (!text_entry_busy() || !game_is_foreground(window)) {
            spring_cursor.suspend();
        }
        previous = buttons;
        remainder_x = remainder_y = 0;
        return_to_scene = false;
        previous_horizontal = previous_vertical = 0;
        repeat_at = 0;
        return;
    }
    if (buttons || aim || state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
        axis(state.Gamepad.sThumbLX) || axis(state.Gamepad.sThumbLY)) {
        controller_active = true;
    }
    const WORD pressed = buttons & ~previous;
    previous = buttons;
    if (return_to_scene) {
        // Let the queued inventory click finish before restoring the aim position.
        leave_inventory(window);
        return_to_scene = false;
        return;
    }
    if (pressed & XINPUT_GAMEPAD_START) {
        spring_cursor.suspend();
        press_key(VK_ESCAPE);
        return;
    }
    if (pressed & XINPUT_GAMEPAD_BACK) {
        if (game::movie_skippable()) {
            press_key(VK_SPACE);
        }
        return;
    }
    const bool all_hotspots = state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    const auto aiming = aim && !current_dialogue() && !inventory_focused(window)
                            ? game::aiming_targets()
                            : std::vector<RECT>{};
    const bool aim_mode = !aiming.empty();
    const bool jump_mode =
        aim_mode ||
        ((all_hotspots ||
          (buttons & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER))) &&
         !current_dialogue() && !inventory_focused(window) && game::world_navigation_available());
    const bool analog_cursor = settings().analog_cursor;
    const auto input = game::current_input();
    if (input != previous_input) {
        spring_cursor.suspend();
        previous_input = input;
    }
    constexpr WORD focus_buttons = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
                                   XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT |
                                   XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER |
                                   XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_RIGHT_THUMB;
    if ((jump_mode && !aim_mode) || !analog_cursor || (buttons & focus_buttons)) {
        spring_cursor.suspend();
    }
    if (analog_cursor && (!jump_mode || aim_mode) &&
        !(settings().spring_cursor && (buttons & focus_buttons))) {
        move_analog_cursor(window, state.Gamepad.sThumbLX, state.Gamepad.sThumbLY, elapsed);
    }
    if (pressed & XINPUT_GAMEPAD_X) {
        click(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
        return;
    }
    if ((pressed & XINPUT_GAMEPAD_RIGHT_THUMB) && focus_conversation_evidence(window)) {
        return;
    }
    const auto horizontal_input =
        (buttons & XINPUT_GAMEPAD_DPAD_RIGHT ? 1 : 0) -
        (buttons & XINPUT_GAMEPAD_DPAD_LEFT ? 1 : 0) +
        ((!analog_cursor || (jump_mode && !aim_mode)) && state.Gamepad.sThumbLX > 18000 ? 1 : 0) -
        ((!analog_cursor || (jump_mode && !aim_mode)) && state.Gamepad.sThumbLX < -18000 ? 1 : 0);
    const auto vertical_input = (buttons & XINPUT_GAMEPAD_DPAD_DOWN ? 1 : 0) -
                                (buttons & XINPUT_GAMEPAD_DPAD_UP ? 1 : 0) +
                                (!analog_cursor && state.Gamepad.sThumbLY < -18000 ? 1 : 0) -
                                (!analog_cursor && state.Gamepad.sThumbLY > 18000 ? 1 : 0);
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
                        (pressed & XINPUT_GAMEPAD_A) != 0, (pressed & XINPUT_GAMEPAD_Y) != 0,
                        (pressed & XINPUT_GAMEPAD_B) != 0)) {
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
    if (pressed & XINPUT_GAMEPAD_B) {
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
    constexpr WORD navigation_buttons =
        XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT |
        XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER;
    const bool selecting = (dialogue || inventory) && (buttons & navigation_buttons);
    if (inventory) {
        const int direction = (pressed & XINPUT_GAMEPAD_DPAD_RIGHT ? 1 : 0) -
                              (pressed & XINPUT_GAMEPAD_DPAD_LEFT ? 1 : 0);
        navigate_inventory(window, direction);
    }
    if (dialogue) {
        const int direction = (pressed & XINPUT_GAMEPAD_DPAD_DOWN ? 1 : 0) -
                              (pressed & XINPUT_GAMEPAD_DPAD_UP ? 1 : 0);
        const int tab =
            (pressed & (XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : 0) -
            (pressed & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_DPAD_LEFT) ? 1 : 0);
        navigate_dialogue(window, direction, tab);
    }
    constexpr float speed = 450;
    constexpr float fine_speed = 90;
    const auto directional_buttons = dialogue || inventory ? WORD{0} : buttons;
    const auto horizontal = (directional_buttons & XINPUT_GAMEPAD_DPAD_RIGHT ? 1 : 0) -
                            (directional_buttons & XINPUT_GAMEPAD_DPAD_LEFT ? 1 : 0);
    const auto vertical = (directional_buttons & XINPUT_GAMEPAD_DPAD_DOWN ? 1 : 0) -
                          (directional_buttons & XINPUT_GAMEPAD_DPAD_UP ? 1 : 0);
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
            ((analog_cursor ? 0 : axis(state.Gamepad.sThumbLX)) * speed + horizontal * fine_speed) *
            elapsed;
        remainder_y +=
            (-(analog_cursor ? 0 : axis(state.Gamepad.sThumbLY)) * speed + vertical * fine_speed) *
            elapsed;
    }
    const auto dx = static_cast<LONG>(remainder_x);
    const auto dy = static_cast<LONG>(remainder_y);
    remainder_x -= dx;
    remainder_y -= dy;
    const bool clicking = (pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X)) != 0;
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
    if (pressed & XINPUT_GAMEPAD_A) {
        click(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
        return_to_scene = inventory;
    }
    if (pressed & XINPUT_GAMEPAD_Y) {
        if (!leave_inventory(window)) {
            close_dialogue(window);
        }
    }
}

}
