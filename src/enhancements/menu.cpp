#include "game/layouts/commands.h"
#include "game/functions/runtime.h"
#include "game/access/ui.h"
#include "enhancements/game_resources.h"
#include "menu.h"
#include "game_ui.h"
#include "script_controls.h"
#include "script_scroll.h"
#include "identity.h"
#include "runtime.h"

#include <vector>

namespace enhancements {
namespace {

std::byte* image = nullptr;
void* pending = nullptr;
const game::Edition* profile = &game::dvd;

native_game::NativeRuntime native() {
    return {image, *profile};
}

}

void attach_menu() {
    std::vector<wchar_t> path(32768);
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) {
        return;
    }
    try {
        const auto identity = identify(path.data());
        if (identity.build && identity.build->profile) {
            profile = identity.build->profile;
            image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        }
    } catch (...) {
        trace_value("menu_resume_unavailable", 1);
    }
}

void update_menu() {
    static std::uintptr_t previous = 0;
    const auto current = game::input_vtable();
    if (current != previous) {
        previous = current;
        trace_value("controller_screen", static_cast<unsigned>(current));
    }
    if (image && pending) {
        const auto app = *native().application().get();
        if (!app || !app->state || native().current_input(app->state) != pending) {
            pending = nullptr;
        }
    }
}

const game::Edition& game::edition() {
    return *profile;
}

std::byte* game::executable_image() {
    return image;
}

void* game::current_input() {
    if (!image) {
        return nullptr;
    }
    const auto app = *native().application().get();
    return app && app->state ? native().current_input(app->state) : nullptr;
}

std::uintptr_t game::input_vtable() {
    const auto input = current_input();
    return input ? reinterpret_cast<std::uintptr_t>(*static_cast<void**>(input)) -
                       reinterpret_cast<std::uintptr_t>(image)
                 : 0;
}

bool game::menu_confirmation_active() {
    return !modal_buttons().empty();
}

bool game::saving_available() {
    // Native Save hover and activation both require this flag.
    return image && *native().scene_active().get() != 0;
}

game::ScriptControls game::script_controls() {
    const auto app = image ? *native().application().get() : nullptr;
    return read_script_controls(app ? app->state : nullptr, image, edition(),
                                app ? app->view : nullptr);
}

game::ScrollResult game::activate_script_button(unsigned resource, const RECT& bounds) {
    const auto app = image ? *native().application().get() : nullptr;
    if (!app || !app->state || !app->view || current_input()) {
        return ScrollResult::unavailable;
    }
    const auto window = GetActiveWindow();
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) {
        return ScrollResult::unavailable;
    }
    if (resource == resource::workstation_photo) {
        return scroll_photo_button(app->state, app->view, image, edition(), bounds);
    }
    return scroll_script_button(app->state, app->view, image, edition(), resource, bounds)
               ? ScrollResult::activated
               : ScrollResult::unavailable;
}

bool resume_from_menu() {
    if (!image) {
        return false;
    }
    const auto app = *native().application().get();
    if (!app || !app->state) {
        return false;
    }
    const auto menu = native().current_input(app->state);
    if (!menu || *static_cast<void**>(menu) != native().main_menu_type()) {
        pending = nullptr;
        return false;
    }
    if (game::menu_confirmation_active()) {
        return true;
    }
    // Previous Game loads a save at startup; only its active-session branch resumes.
    if (menu == pending || !*native().session_active().get() || !*native().scene_active().get()) {
        return true;
    }
    pending = menu;
    const auto queue = native_game::ApplicationRef(app).commands().data();
    native().activate(queue, 0, menu);
    native().remove(queue, menu);
    native().pause(app->state, 0, queue);
    *native().menu_used().get() = 1;
    native().restore_preferences(app->preferences, queue);
    native().restore_view(app->view);
    trace_value("menu_resume", 1);
    return true;
}

}
