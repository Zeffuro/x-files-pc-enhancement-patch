#include "input_source.h"
#include "enhancements/game_resources.h"
#include "login.h"
#include "focus.h"
#include "game_ui.h"
#include "settings.h"

#include <algorithm>

namespace enhancements {

void update_login(HWND window, bool focused) {
    static ULONGLONG entered = 0;
    static bool attempted = false;
    static bool restore = false;
    static POINT previous{};
    constexpr RECT shortcut{98, 44, 114, 64};
    constexpr POINT target{106, 54};
    if (!focused || !settings().skip_workstation_login) {
        entered = 0;
        restore = false;
        return;
    }
    const auto script = game::script_controls();
    const auto has = [&](unsigned id) {
        return std::find(script.resources.begin(), script.resources.end(), id) !=
               script.resources.end();
    };
    const bool login = !game::current_input() && has(resource::workstation_login) &&
                       !has(resource::incorrect_password);
    POINT cursor{};
    const bool have_cursor = GetCursorPos(&cursor) && ScreenToClient(window, &cursor);
    if (restore && (!have_cursor || cursor.x != target.x || cursor.y != target.y)) {
        restore = false;
    }
    if (!login) {
        if (restore && ClientToScreen(window, &previous)) {
            move_controller_pointer(previous.x, previous.y);
        }
        entered = 0;
        attempted = restore = false;
        return;
    }
    const auto now = GetTickCount64();
    if (!entered) {
        entered = now;
    }
    if (now - entered > 1500) {
        restore = false;
    }
    if (!attempted && have_cursor && now - entered >= 250) {
        attempted = true;
        previous = cursor;
        restore = point_controller(window, shortcut, true, true);
    }
}

}
