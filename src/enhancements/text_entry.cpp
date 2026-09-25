#include "enhancements/game_resources.h"
#include "text_entry.h"
#include "controls.h"
#include "focus.h"
#include "ui/highlight.h"
#include "ui/keyboard_view.h"
#include "game_ui.h"
#include "settings.h"

#include <xinput.h>
#include <algorithm>
#include <deque>

namespace enhancements {
namespace {
constexpr ULONG_PTR input_tag = 0x58465458;

struct Keyboard {
    HWND owner = nullptr;
    RECT field{};
    unsigned resource = 0, selected = 10;
    WORD previous_buttons = 0;
    int horizontal = 0, vertical = 0;
    ULONGLONG repeat = 0, last = 0;
    bool opened = false;
} keyboard;

std::deque<char> pending;
ULONGLONG send_at = 0, closed_until = 0;

WORD buttons(XINPUT_STATE& state) {
    for (DWORD player = 0; player < XUSER_MAX_COUNT; ++player) {
        if (XInputGetState(player, &state) == ERROR_SUCCESS) {
            return state.Gamepad.wButtons;
        }
    }
    state = {};
    return 0;
}

void finish() {
    suspend_analog_cursor();
    keyboard.opened = false;
    hide_keyboard();
    closed_until = GetTickCount64() + 250;
}

bool same_screen(const game::ScriptControls& controls) {
    return !game::current_input() && std::find(controls.resources.begin(), controls.resources.end(),
                                               keyboard.resource) != controls.resources.end();
}

void activate(unsigned index) {
    if (pending.size() > 128) {
        return;
    }
    if (index < keyboard_letter_count) {
        pending.push_back(keyboard_letters[index]);
    } else if (index == keyboard_letter_count) {
        pending.push_back(' ');
    } else if (index == keyboard_letter_count + 1) {
        pending.push_back('\b');
    } else if (index == keyboard_letter_count + 2) {
        if (!pending.empty()) {
            return;
        }
        for (const auto& field : game::script_controls().fields) {
            if (field.bounds.left >= keyboard.field.left &&
                field.bounds.top >= keyboard.field.top &&
                field.bounds.right <= keyboard.field.right &&
                field.bounds.bottom <= keyboard.field.bottom) {
                pending.insert(pending.end(), field.value.size(), '\b');
                break;
            }
        }
    } else {
        finish();
    }
}

void move(int horizontal, int vertical) {
    const auto bounds = keyboard_bounds();
    const auto& selected = bounds[keyboard.selected];
    const POINT cursor{(selected.left + selected.right) / 2, (selected.top + selected.bottom) / 2};
    const auto target = directional_target(bounds, cursor, horizontal, vertical);
    if (target >= 0) {
        keyboard.selected = static_cast<unsigned>(target);
        point_controller(keyboard.owner, bounds[keyboard.selected]);
    }
}

void poll() {
    XINPUT_STATE state{};
    const auto current = buttons(state);
    const auto pressed = current & ~keyboard.previous_buttons;
    keyboard.previous_buttons = current;
    const auto now = GetTickCount64();
    const float elapsed = keyboard.last ? std::min(0.05f, (now - keyboard.last) / 1000.0f) : 0;
    keyboard.last = now;
    if (!settings().gamepad) {
        return;
    }
    if (pressed & (XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_START)) {
        finish();
        return;
    }
    if (pressed & XINPUT_GAMEPAD_X) {
        pending.push_back('\b');
    }
    const bool analog = settings().analog_cursor;
    const bool selecting = (current & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
                                       XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) != 0;
    if (selecting) {
        suspend_analog_cursor();
    }
    if (analog && !(selecting && settings().spring_cursor)) {
        move_analog_cursor(keyboard.owner, state.Gamepad.sThumbLX, state.Gamepad.sThumbLY, elapsed);
    }
    const int horizontal = std::clamp((current & XINPUT_GAMEPAD_DPAD_RIGHT ? 1 : 0) -
                                          (current & XINPUT_GAMEPAD_DPAD_LEFT ? 1 : 0) +
                                          (!analog && state.Gamepad.sThumbLX > 18000 ? 1 : 0) -
                                          (!analog && state.Gamepad.sThumbLX < -18000 ? 1 : 0),
                                      -1, 1);
    const int vertical = std::clamp((current & XINPUT_GAMEPAD_DPAD_DOWN ? 1 : 0) -
                                        (current & XINPUT_GAMEPAD_DPAD_UP ? 1 : 0) +
                                        (!analog && state.Gamepad.sThumbLY < -18000 ? 1 : 0) -
                                        (!analog && state.Gamepad.sThumbLY > 18000 ? 1 : 0),
                                    -1, 1);
    const bool changed = horizontal != keyboard.horizontal || vertical != keyboard.vertical;
    if ((horizontal || vertical) && (changed || now >= keyboard.repeat)) {
        move(horizontal, vertical);
        keyboard.repeat = now + (changed ? 350 : 140);
    }
    keyboard.horizontal = horizontal;
    keyboard.vertical = vertical;
    if (pressed & XINPUT_GAMEPAD_A) {
        activate(keyboard.selected);
    }
}

void send_character() {
    if (pending.empty() || GetTickCount64() < send_at) {
        return;
    }
    const auto key = VkKeyScanA(pending.front());
    pending.pop_front();
    if (key != -1) {
        INPUT events[4]{};
        unsigned count = 0;
        const auto add = [&](WORD code, DWORD flags) {
            auto& event = events[count++];
            event.type = INPUT_KEYBOARD;
            event.ki.wVk = code;
            event.ki.dwFlags = flags;
            event.ki.dwExtraInfo = input_tag;
        };
        const bool shift = (key & 0x100) != 0;
        if (shift) {
            add(VK_SHIFT, 0);
        }
        add(LOBYTE(key), 0);
        add(LOBYTE(key), KEYEVENTF_KEYUP);
        if (shift) {
            add(VK_SHIFT, KEYEVENTF_KEYUP);
        }
        SendInput(count, events, sizeof(INPUT));
    }
    send_at = GetTickCount64() + 35;
    if (!keyboard.opened) {
        closed_until = send_at + 100;
    }
}
}

bool text_entry_busy() {
    return keyboard.opened || !pending.empty() || GetTickCount64() < closed_until;
}

void open_text_entry(HWND owner, const RECT& field, unsigned resource) {
    if (text_entry_busy()) {
        return;
    }
    suspend_analog_cursor();
    keyboard = {};
    keyboard.owner = owner;
    keyboard.field = field;
    keyboard.resource = resource;
    XINPUT_STATE state{};
    keyboard.previous_buttons = buttons(state);
    if (!point_controller(owner, field)) {
        return;
    }
    INPUT events[2]{};
    for (auto& event : events) {
        event.type = INPUT_MOUSE;
        event.mi.dwExtraInfo = input_tag;
    }
    events[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    events[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    if (SendInput(2, events, sizeof(INPUT)) != 2) {
        return;
    }
    keyboard.opened = true;
    send_at = GetTickCount64() + 150;
    update_highlight(owner, false);
    show_keyboard(owner, resource == resource::save, keyboard.selected);
}

bool text_entry_message(UINT message, WPARAM value, LPARAM) {
    const bool key = message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR;
    const bool mouse = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
    // Let our field click and characters reach the game's native editor.
    if ((key || mouse) && static_cast<ULONG_PTR>(GetMessageExtraInfo()) == input_tag) {
        return false;
    }
    if (!keyboard.opened) {
        return (key || mouse) && GetTickCount64() < closed_until;
    }
    if (message == WM_KEYDOWN) {
        if (value == VK_ESCAPE || value == VK_RETURN) {
            finish();
            return true;
        }
        const int horizontal = (value == VK_RIGHT) - (value == VK_LEFT);
        const int vertical = (value == VK_DOWN) - (value == VK_UP);
        if (horizontal || vertical) {
            suspend_analog_cursor();
            move(horizontal, vertical);
            return true;
        }
    } else if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN) {
        POINT cursor{};
        if (!GetCursorPos(&cursor) || !ScreenToClient(keyboard.owner, &cursor)) {
            return true;
        }
        const auto bounds = keyboard_bounds();
        for (unsigned i = 0; i < bounds.size(); ++i) {
            if (PtInRect(&bounds[i], cursor)) {
                keyboard.selected = i;
                if (message == WM_LBUTTONDOWN) {
                    activate(i);
                }
                break;
            }
        }
    }
    return mouse ||
           (key && (value == VK_TAB || value == VK_RETURN || value == VK_ESCAPE ||
                    value == VK_LEFT || value == VK_RIGHT || value == VK_UP || value == VK_DOWN));
}

void update_text_entry(HWND owner, bool focused) {
    if (!keyboard.opened && pending.empty()) {
        return;
    }
    if (!same_screen(game::script_controls())) {
        finish();
        pending.clear();
        return;
    }
    if (!focused) {
        if (keyboard.opened && !IsIconic(owner)) {
            show_keyboard(owner, keyboard.resource == resource::save, keyboard.selected);
        } else {
            hide_keyboard();
        }
        pending.clear();
        XINPUT_STATE state{};
        keyboard.previous_buttons = buttons(state);
        keyboard.last = 0;
        return;
    }
    if (keyboard.opened) {
        poll();
        if (keyboard.opened) {
            show_keyboard(owner, keyboard.resource == resource::save, keyboard.selected);
        }
    }
    send_character();
}

void release_text_entry() {
    keyboard = {};
    pending.clear();
    release_keyboard();
}
}
