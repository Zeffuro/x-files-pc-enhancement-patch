#include "enhancements/game_resources.h"
#include "menu.h"
#include "game_ui.h"
#include "identity.h"
#include "runtime.h"

#include <cstring>
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
        if (identity.edition && game::edition_named(identity.edition)) {
            profile = game::edition_named(identity.edition);
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
    ScriptControls result;
    if (!image) {
        return result;
    }
    const auto app = *address<Application**>(edition().application);
    if (!app || !app->state) {
        return result;
    }
    const auto state = static_cast<std::byte*>(app->state);
    const auto collect = [&](std::size_t offset, auto callback) {
        const auto& list = *reinterpret_cast<List<std::byte>*>(state + offset);
        if (list.count > 256) {
            return;
        }
        auto node = list.first;
        for (unsigned index = 0; node && index < list.count; ++index, node = node->next) {
            if (node->value) {
                callback(node->value);
            }
        }
    };
    collect(0x25c, [&](std::byte* object) {
        if (*reinterpret_cast<void**>(object) == address<void*>(game::edition().script_root)) {
            const auto resource = *reinterpret_cast<std::byte**>(object + 0x18);
            if (resource) {
                result.resources.push_back(*reinterpret_cast<unsigned*>(resource + 4));
            }
        }
    });
    collect(0x270, [&](std::byte* object) {
        if (*reinterpret_cast<void**>(object) != address<void*>(game::edition().script_control)) {
            return;
        }
        const auto id = *reinterpret_cast<unsigned*>(object + 0x144);
        if (id == script_control::text_cursor || id == script_control::dialog_text) {
            result.text_input = true;
        }
        const auto rectangle =
            reinterpret_cast<Rectangle*>(object + edition().control_rectangle)->bounds;
        if (id == script_control::dialog_background) {
            result.script_dialog = true;
        }
        if (id == script_control::auxiliary_first && rectangle.left >= 0 && rectangle.top >= 0 &&
            rectangle.right <= 640 && rectangle.bottom <= 480 && rectangle.right > rectangle.left &&
            rectangle.bottom > rectangle.top) {
            if (*reinterpret_cast<unsigned*>(object + 0x20)) {
                result.dialog_buttons.push_back(rectangle);
            } else if (!*reinterpret_cast<void**>(object + 0x140) &&
                       rectangle.right - rectangle.left > 50) {
                result.dialog_fields.push_back(rectangle);
            }
        }
        if (id == script_control::acknowledgement && *reinterpret_cast<unsigned*>(object + 0x20)) {
            const auto bounds =
                reinterpret_cast<Rectangle*>(object + edition().control_rectangle)->bounds;
            if (bounds.left >= 0 && bounds.top >= 0 && bounds.right <= 640 &&
                bounds.bottom <= 480 && bounds.right > bounds.left && bounds.bottom > bounds.top) {
                result.acknowledgement_buttons.push_back(bounds);
            }
        }
        if (!script_control::selectable(id) && id != script_control::dialog_text) {
            return;
        }
        const auto bounds =
            reinterpret_cast<Rectangle*>(object + edition().control_rectangle)->bounds;
        if (bounds.left >= 0 && bounds.top >= 0 && bounds.right <= 640 && bounds.bottom <= 480 &&
            bounds.right > bounds.left && bounds.bottom > bounds.top &&
            (bounds.right - bounds.left < 640 || bounds.bottom - bounds.top < 480)) {
            const auto graphic = *reinterpret_cast<std::byte**>(object + 0x140);
            if (graphic && *reinterpret_cast<void**>(graphic) ==
                               address<void*>(game::edition().input_graphic)) {
                const auto field = *reinterpret_cast<std::byte**>(graphic + 0x154);
                if (field &&
                    *reinterpret_cast<void**>(field) == address<void*>(game::edition().text)) {
                    const auto text = *reinterpret_cast<char**>(field + 0x28);
                    if (text) {
                        result.fields.push_back({bounds, std::string(text, strnlen(text, 128))});
                    }
                    if (text && *text) {
                        result.text.push_back(bounds);
                    }
                }
            }
            if (reinterpret_cast<List<void>*>(object + 0x1c)->count) {
                result.buttons.push_back(bounds);
            } else if (id == script_control::hover) {
                result.hover_buttons.push_back(bounds);
            }
        }
    });
    return result;
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
