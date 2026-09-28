#include "enhancements/game_resources.h"
#include "menu.h"
#include "game_ui.h"
#include "script_controls.h"
#include "identity.h"
#include "runtime.h"

#include <vector>

namespace enhancements {
namespace {

using CurrentInput = void*(__stdcall*)(void*);
using Activate = void(__stdcall*)(void*, int, void*);
using Remove = void(__stdcall*)(void*, void*);
using Pause = void(__stdcall*)(void*, int, void*);
using RestorePreferences = void(__stdcall*)(void*, void*);
using RestoreView = void(__stdcall*)(game::MainView*);

std::byte* image = nullptr;
void* pending = nullptr;
const game::Edition* profile = &game::dvd;

template <typename T> T address(std::size_t rva) {
    return reinterpret_cast<T>(image + rva);
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
        const auto app = *address<game::Application**>(game::edition().application);
        if (!app || !app->state ||
            address<CurrentInput>(game::edition().current_input)(app->state) != pending) {
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
    const auto app = *address<game::Application**>(game::edition().application);
    return app && app->state ? address<CurrentInput>(game::edition().current_input)(app->state)
                             : nullptr;
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
    return image && *address<int*>(edition().scene_active) != 0;
}

game::ScriptControls game::script_controls() {
    const auto app = image ? *address<Application**>(edition().application) : nullptr;
    return read_script_controls(app ? app->state : nullptr, image, edition());
}

bool resume_from_menu() {
    if (!image) {
        return false;
    }
    const auto app = *address<game::Application**>(game::edition().application);
    if (!app || !app->state) {
        return false;
    }
    const auto menu = address<CurrentInput>(game::edition().current_input)(app->state);
    if (!menu || *static_cast<void**>(menu) != address<void*>(game::edition().main_menu)) {
        pending = nullptr;
        return false;
    }
    if (game::menu_confirmation_active()) {
        return true;
    }
    // Previous Game loads a save at startup; only its active-session branch resumes.
    if (menu == pending || !*address<int*>(game::edition().session_active) ||
        !*address<int*>(game::edition().scene_active)) {
        return true;
    }
    pending = menu;
    const auto queue = reinterpret_cast<std::byte*>(app) + 0x268;
    address<Activate>(game::edition().activate)(queue, 0, menu);
    address<Remove>(game::edition().remove)(queue, menu);
    address<Pause>(game::edition().pause)(app->state, 0, queue);
    *address<int*>(game::edition().menu_used) = 1;
    address<RestorePreferences>(game::edition().restore_preferences)(app->preferences, queue);
    address<RestoreView>(game::edition().restore_view)(app->view);
    trace_value("menu_resume", 1);
    return true;
}

}
