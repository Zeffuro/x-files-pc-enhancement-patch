#include "gun.h"
#include "game_ui.h"
#include "dialogue.h"
#include "game_resources.h"

namespace enhancements {
namespace {

thread_local bool equipping = false;
constexpr int gun_action = 9;

template <class T> bool read(const void* address, T& value) {
    SIZE_T bytes = 0;
    return address &&
           ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &bytes) &&
           bytes == sizeof(value);
}

const void* field(const void* object, std::size_t offset) {
    return reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(object) + offset);
}

struct Object {
    void* vtable;
    unsigned visible, enabled, disabled;
};

template <class Callback> bool visit(const void* address, Callback callback) {
    game::List<std::byte> list{};
    if (!read(address, list) || list.count > 256 || (!list.count && (list.first || list.last))) {
        return false;
    }
    auto node = list.first;
    game::List<std::byte>::Node* previous = nullptr;
    for (unsigned i = 0; i < list.count; ++i) {
        game::List<std::byte>::Node item{};
        if (!read(node, item) || !item.value || item.previous != previous ||
            !callback(item.value)) {
            return false;
        }
        previous = node;
        node = item.next;
    }
    return !node && previous == list.last;
}

bool action_list(const void* address) {
    bool present = false;
    return visit(address,
                 [&](std::byte* action) {
                     void* vtable = nullptr;
                     present = true;
                     return read(action, vtable) && vtable;
                 }) &&
           present;
}

struct Selection {
    game::Application* application = nullptr;
    game::Application app{};
    void* resource = nullptr;
    void* selected = nullptr;
    void* variable = nullptr;
    int mode = 0;
};

bool inspect(std::byte* image, const game::Edition& profile, Selection& selection) {
    unsigned scene = 0, registered_action = 0, queued = 0;
    std::uint8_t variable_type = 0;
    if (!image || !read(image + profile.application, selection.application) ||
        !read(selection.application, selection.app) || !selection.app.state ||
        !selection.app.view || !read(image + profile.scene_active, scene) || !scene ||
        !read(image + profile.registered_gun_action, registered_action) || !registered_action ||
        !read(image + profile.inventory_action_variable, selection.variable) ||
        !read(field(selection.variable, 0x41), variable_type) || (variable_type & 0x7f) != 1 ||
        !read(field(selection.variable, 0x38), selection.mode) ||
        !read(field(selection.application, 0x26c), queued) || queued) {
        return false;
    }
    unsigned matches = 0;
    const bool valid = visit(field(selection.app.state, 0x144), [&](std::byte* item) {
        Object object{};
        if (!read(item, object) || object.vtable != image + profile.owned_inventory_item) {
            return false;
        }
        void* graphic = nullptr;
        void* graphic_resource = nullptr;
        unsigned id = 0;
        if (!read(field(item, 0x1e4), graphic) || !read(field(graphic, 0x18), graphic_resource) ||
            !read(field(graphic_resource, 4), id)) {
            return false;
        }
        if (id != resource::inventory_gun) {
            return true;
        }
        ++matches;
        unsigned action = 0;
        return object.enabled && !object.disabled && read(field(item, 0x18), selection.resource) &&
               read(field(selection.resource, 0x38), action) && action == registered_action &&
               action_list(field(item, 0x1c));
    });
    if (!valid || matches != 1) {
        return false;
    }
    // The selector consults both resources and the existing native action lists.
    unsigned selected_action = 0;
    return read(image + profile.selected_inventory, selection.selected) &&
           (!selection.selected || read(field(selection.selected, 0x38), selected_action)) &&
           visit(field(selection.app.state, 0x1a8),
                 [](std::byte* item) {
                     Object object{};
                     return read(item, object) && object.vtable;
                 }) &&
           visit(field(selection.app.state, 0x1e4), [](std::byte* item) {
               Object object{};
               return read(item, object) && object.vtable;
           });
}

bool unchanged(std::byte* image, const game::Edition& profile, const Selection& before) {
    game::Application* application = nullptr;
    game::Application app{};
    return read(image + profile.application, application) && application == before.application &&
           read(application, app) && app.state == before.app.state && app.view == before.app.view;
}

}

bool controller_gun_busy() {
    return equipping;
}

void refresh_controller_gun_cursor(POINT position) {
    refresh_native_cursor(position);
}

bool refresh_native_cursor(POINT position) {
    const auto image = game::executable_image();
    const auto& profile = game::edition();
    game::Application* application = nullptr;
    game::Application app{};

    struct Queue {
        void* vtable;
        unsigned count, current;
        void* storage;
        unsigned capacity;
    } queue{};

    if (!image || !read(image + profile.application, application) || !read(application, app) ||
        !app.state || !app.view || !read(field(application, 0x280), queue) || !queue.vtable ||
        !queue.storage || queue.capacity > 256 || queue.count >= queue.capacity) {
        return false;
    }

    struct Point {
        void* vtable;
        POINT value;
    } point{nullptr, position};

    static_assert(sizeof(Point) == 12);
    // WM_MOUSEMOVE ignores unchanged positions. Native events also clear an old hover target.
    using Move = void(__stdcall*)(void*, const Point*, const Point*);
    reinterpret_cast<Move>(image + profile.queue_mouse_move)(application, &point, &point);
    return true;
}

bool equip_controller_gun() {
    if (equipping) {
        return false;
    }
    const auto image = game::executable_image();
    const auto& profile = game::edition();
    Selection selection;
    if (!inspect(image, profile, selection) || game::menu_confirmation_active() ||
        current_dialogue() ||
        (!game::world_navigation_available() && game::aiming_targets().empty())) {
        return false;
    }
    if (selection.mode == gun_action && selection.selected == selection.resource) {
        return true;
    }

    struct Guard {
        Guard() {
            equipping = true;
        }

        ~Guard() {
            equipping = false;
        }
    } guard;

    using Select = void(__stdcall*)(void*, void*);
    using SetAction = void(__stdcall*)(int, void*);
    auto queue = reinterpret_cast<std::byte*>(selection.application) + 0x268;
    // Both calls append native events. Asset loading can reenter controller polling.
    reinterpret_cast<Select>(image + profile.select_inventory)(selection.resource, queue);
    if (!unchanged(image, profile, selection)) {
        return false;
    }
    reinterpret_cast<SetAction>(image + profile.set_inventory_action)(gun_action, queue);
    int mode = 0;
    return unchanged(image, profile, selection) && read(field(selection.variable, 0x38), mode) &&
           mode == gun_action;
}

}
