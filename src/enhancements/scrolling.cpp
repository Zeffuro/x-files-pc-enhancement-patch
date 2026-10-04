#include "scrolling.h"
#include "controls.h"
#include "dialogue.h"
#include "documents.h"
#include "game_resources.h"
#include "inventory.h"
#include "text_entry.h"
#include "ui/quick_menu.h"
#include "saves/browser.h"
#include "transcript/view.h"

#include <windowsx.h>

namespace enhancements::scrolling {
namespace {

struct Context {
    game::MainView* view = nullptr;
    void* input = nullptr;
    unsigned resource = 0;
    bool history = false;
    Target target;
};

thread_local Wheel wheel;
thread_local Context previous;
thread_local HWND scroll_window = nullptr;
thread_local int queued = 0;
thread_local bool dispatching = false;

bool same(const Context& a, const Context& b) {
    return a.view == b.view && a.input == b.input && a.resource == b.resource &&
           a.history == b.history && EqualRect(&a.target.content, &b.target.content) &&
           EqualRect(&a.target.up, &b.target.up) && EqualRect(&a.target.down, &b.target.down);
}

Context context() {
    Context result;
    result.view = game::current_view();
    result.input = game::current_input();
    if (!result.view || !game::modal_buttons().empty() || game::menu_confirmation_active() ||
        text_entry_busy() || quick_menu::expanded() || documents::active() ||
        transcript::active() || saves::browser_active()) {
        return {};
    }
    if (const auto frame = current_dialogue()) {
        result.history = frame->is_history;
        result.resource = 1;
        result.target.content = frame->viewport;
    } else if (!game::input_vtable()) {
        result.target = device_target(game::script_controls());
        result.resource = result.target.resource;
    }
    return result;
}

bool pointer_at(HWND window, POINT& point) {
    return GetCursorPos(&point) && ScreenToClient(window, &point);
}

void dispatch() {
    if (dispatching || !scroll_window) {
        return;
    }
    dispatching = true;
    while (queued) {
        const auto current = context();
        POINT point{};
        if (!same(current, previous) || !pointer_at(scroll_window, point) ||
            !PtInRect(&current.target.content, point) || !game_is_foreground(scroll_window) ||
            controller_inventory_click_pending() || inventory_focused(scroll_window) ||
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000)) {
            reset();
            break;
        }
        const int direction = queued > 0 ? -1 : 1;
        queued += queued > 0 ? -1 : 1;
        const bool activated =
            current.resource == 1
                ? scroll_dialogue(direction)
                : game::activate_script_button(
                      current.resource, direction < 0 ? current.target.up : current.target.down);
        // Native actions can replace the page. Reacquire before another detent.
        if (!activated || !same(context(), previous)) {
            reset();
            break;
        }
    }
    dispatching = false;
}

}

Target device_target(const game::ScriptControls& script) {
    if (script.script_dialog || !script.acknowledgement_buttons.empty()) {
        return {};
    }
    const auto has = [&](unsigned resource) {
        return std::find(script.resources.begin(), script.resources.end(), resource) !=
               script.resources.end();
    };
    if (has(resource::options) || has(resource::save) || has(resource::load) ||
        has(resource::help) || has(resource::phone)) {
        return {};
    }
    Target result;
    POINT up{}, down{};
    if (has(resource::pda_notes)) {
        result = {resource::pda_notes, {225, 87, 410, 350}};
        up = {422, 95};
        down = {422, 339};
    } else if (has(resource::pda_message)) {
        result = {resource::pda_message, {224, 55, 416, 362}};
        up = {428, 62};
        down = {428, 351};
    } else if (has(resource::workstation_message)) {
        result = {resource::workstation_message, {151, 93, 598, 451}};
        up = {607, 107};
        down = {607, 443};
    } else {
        return {};
    }
    for (const auto& bounds : script.buttons) {
        if (bounds.right - bounds.left > 32 || bounds.bottom - bounds.top > 32 ||
            IsRectEmpty(&bounds)) {
            continue;
        }
        if (PtInRect(&bounds, up)) {
            result.up = bounds;
        }
        if (PtInRect(&bounds, down)) {
            result.down = bounds;
        }
    }
    return IsRectEmpty(&result.up) || IsRectEmpty(&result.down) ? Target{} : result;
}

bool message(HWND window, UINT message, WPARAM value, LPARAM) {
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_RBUTTONDOWN ||
        message == WM_MBUTTONDOWN || message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
        message == WM_LBUTTONUP) {
        reset();
    }
    if (message != WM_MOUSEWHEEL) {
        return false;
    }
    const auto current = context();
    POINT point{};
    if (dispatching || !current.resource || !game_is_foreground(window) ||
        !pointer_at(window, point) ||
        (GET_KEYSTATE_WPARAM(value) & (MK_CONTROL | MK_SHIFT | MK_LBUTTON | MK_RBUTTON)) ||
        inventory_focused(window) || !PtInRect(&current.target.content, point)) {
        reset();
        return false;
    }
    if (!same(current, previous) || window != scroll_window) {
        reset();
    }
    previous = current;
    scroll_window = window;
    queued = std::clamp(queued + wheel.add(GET_WHEEL_DELTA_WPARAM(value)), -8, 8);
    dispatch();
    return true;
}

void reset() {
    wheel.reset();
    previous = {};
    scroll_window = nullptr;
    queued = 0;
}

void update(bool available) {
    POINT point{};
    if (!available ||
        (scroll_window && (!same(context(), previous) || !pointer_at(scroll_window, point) ||
                           !PtInRect(&previous.target.content, point)))) {
        reset();
    }
}

}
