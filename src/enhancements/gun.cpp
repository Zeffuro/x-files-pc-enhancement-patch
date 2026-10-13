#include "gun.h"
#include "game_ui.h"
#include "dialogue.h"
#include "game_resources.h"
#include "game/layouts/native_objects.h"
#include "game/layouts/database/variable.h"
#include "game/layouts/input/events.h"
#include "game/layouts/commands.h"
#include "game/layouts/script.h"
#include "game/layouts/input/inventory_selection.h"
#include "game/access/world.h"
#include "game/access/database.h"
#include "game/functions/runtime.h"

namespace enhancements {
namespace {

thread_local bool equipping = false;
constexpr auto gun_action = native_game::inventory_selection::gun;

template <class T> bool read(const void* address, T& value) {
    SIZE_T bytes = 0;
    return address &&
           ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &bytes) &&
           bytes == sizeof(value);
}

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
    if (!image ||
        !read(native_game::NativeRuntime(image, profile).application().data(),
              selection.application) ||
        !read(selection.application, selection.app) || !selection.app.state ||
        !selection.app.view ||
        !read(native_game::NativeRuntime(image, profile).scene_active().data(), scene) || !scene ||
        !read(native_game::NativeRuntime(image, profile).registered_gun_action().data(),
              registered_action) ||
        !registered_action ||
        !read(native_game::NativeRuntime(image, profile).inventory_action_variable().data(),
              selection.variable) ||
        !read(native_game::VariableRef(selection.variable).type_flags().data(), variable_type) ||
        (variable_type & native_game::variable_type_mask) !=
            static_cast<unsigned>(native_game::VariableKind::integer) ||
        !read(native_game::VariableRef(selection.variable).raw_value().data(), selection.mode) ||
        !read(native_game::ApplicationRef(selection.application).commands().count().data(),
              queued) ||
        queued) {
        return false;
    }
    unsigned matches = 0;
    const bool valid = visit(
        native_game::SceneStateRef(selection.app.state).owned_inventory().data(),
        [&](std::byte* item) {
            native_game::ObjectFlags object{};
            if (!read(item, object) ||
                object.type !=
                    native_game::NativeRuntime(image, profile).owned_inventory_item_type()) {
                return false;
            }
            void* graphic = nullptr;
            void* graphic_resource = nullptr;
            unsigned id = 0;
            if (!read(native_game::OwnedInventoryItemRef(item).graphic().data(), graphic) ||
                !read(native_game::GraphicRef(graphic).resource().data(), graphic_resource) ||
                !read(native_game::ResourceHeaderRef(graphic_resource).id().data(), id)) {
                return false;
            }
            if (id != resource::inventory_gun) {
                return true;
            }
            ++matches;
            unsigned action = 0;
            return object.enabled && !object.blocked &&
                   read(native_game::ResourceObjectRef(item).resource().data(),
                        selection.resource) &&
                   read(native_game::InventoryActionResourceRef(selection.resource).action().data(),
                        action) &&
                   action == registered_action &&
                   action_list(native_game::InputEventsRef(item).actions().data());
        });
    if (!valid || matches != 1) {
        return false;
    }
    // The selector consults both resources and the existing native action lists.
    unsigned selected_action = 0;
    return read(native_game::NativeRuntime(image, profile).selected_inventory().data(),
                selection.selected) &&
           (!selection.selected ||
            read(native_game::InventoryActionResourceRef(selection.selected).action().data(),
                 selected_action)) &&
           visit(native_game::SceneStateRef(selection.app.state).selection_first().data(),
                 [](std::byte* item) {
                     native_game::ObjectFlags object{};
                     return read(item, object) && object.type;
                 }) &&
           visit(native_game::SceneStateRef(selection.app.state).selection_second().data(),
                 [](std::byte* item) {
                     native_game::ObjectFlags object{};
                     return read(item, object) && object.type;
                 });
}

bool unchanged(std::byte* image, const game::Edition& profile, const Selection& before) {
    game::Application* application = nullptr;
    game::Application app{};
    return read(native_game::NativeRuntime(image, profile).application().data(), application) &&
           application == before.application && read(application, app) &&
           app.state == before.app.state && app.view == before.app.view;
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

    native_game::CommandQueuePrefix queue{};

    if (!image ||
        !read(native_game::NativeRuntime(image, profile).application().data(), application) ||
        !read(application, app) || !app.state || !app.view ||
        !read(native_game::ApplicationRef(application).pointer_events().data(), queue) ||
        !queue.vtable || !queue.storage || queue.capacity > 256 || queue.count >= queue.capacity) {
        return false;
    }

    native_game::NativePoint point{nullptr, position};
    // WM_MOUSEMOVE ignores unchanged positions. Native events also clear an old hover target.
    native_game::NativeFunctions(image, profile).queue_mouse_move(application, &point, &point);
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

    auto queue = native_game::ApplicationRef(selection.application).commands().data();
    // Both calls append native events. Asset loading can reenter controller polling.
    native_game::NativeFunctions(image, profile).select_inventory(selection.resource, queue);
    if (!unchanged(image, profile, selection)) {
        return false;
    }
    native_game::NativeFunctions(image, profile).set_inventory_action(gun_action, queue);
    int mode = 0;
    return unchanged(image, profile, selection) &&
           read(native_game::VariableRef(selection.variable).raw_value().data(), mode) &&
           mode == gun_action;
}

}
