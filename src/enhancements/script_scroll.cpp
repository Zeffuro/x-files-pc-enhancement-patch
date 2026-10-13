#include "game/functions/runtime.h"
#include "script_scroll.h"
#include "game_resources.h"
#include "game/layouts/script.h"
#include "game/layouts/script_geometry.h"
#include "game/access/scrolling.h"

#include <array>
#include <limits>

namespace enhancements::game {
namespace {
thread_local bool photo_dispatching = false;
constexpr unsigned maximum_native_entries = 256;
constexpr unsigned minimum_command_capacity = 10;
constexpr unsigned maximum_command_capacity = 65536;

template <class T> bool read(const void* address, T& value) {
    SIZE_T copied = 0;
    return address &&
           ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &copied) &&
           copied == sizeof(value);
}

template <class T, class F> bool visit(const void* address, F callback) {
    List<T> list{};
    if (!read(address, list) || list.count > maximum_native_entries) {
        return false;
    }
    auto node = list.first;
    typename List<T>::Node* previous = nullptr;
    std::vector<typename List<T>::Node*> visited;
    for (unsigned i = 0; i < list.count; ++i) {
        typename List<T>::Node record{};
        if (!node || std::find(visited.begin(), visited.end(), node) != visited.end() ||
            !read(node, record) || record.previous != previous || !record.value ||
            !callback(record.value)) {
            return false;
        }
        visited.push_back(node);
        previous = node;
        node = record.next;
    }
    return !node && previous == list.last;
}

bool type(const void* object, const void* expected) {
    void* actual = nullptr;
    return read(object, actual) && actual == expected;
}

bool rectangle(const RECT& bounds) {
    return bounds.left >= 0 && bounds.top >= 0 &&
           bounds.right <= native_game::script_geometry::logical_size.cx &&
           bounds.bottom <= native_game::script_geometry::logical_size.cy &&
           bounds.right > bounds.left && bounds.bottom > bounds.top;
}

bool resources(std::byte* state, std::byte* image, const Edition& profile, unsigned wanted) {
    bool found = false, blocked = false;
    const bool valid =
        visit<std::byte>(native_game::ScriptStateRef(state).roots().data(), [&](std::byte* object) {
            if (!type(object, native_game::NativeRuntime(image, profile).script_root_type())) {
                return true;
            }
            std::byte* resource = nullptr;
            unsigned id = 0;
            if (!read(native_game::ScriptInputRef(object).resource().data(), resource) ||
                !resource || !read(native_game::ResourceHeaderRef(resource).id().data(), id)) {
                return false;
            }
            found |= id == wanted;
            blocked |= id == resource::options || id == resource::save || id == resource::load ||
                       id == resource::help || id == resource::phone;
            return true;
        });
    return valid && found && !blocked;
}

bool arrows(std::byte* state, std::byte* image, const Edition& profile, const RECT& bounds,
            POINT up, POINT down, int& direction) {
    unsigned up_count = 0, down_count = 0, selected_count = 0;
    const bool valid = visit<std::byte>(
        native_game::ScriptStateRef(state).controls().data(), [&](std::byte* object) {
            if (!type(object, native_game::NativeRuntime(image, profile).script_control_type())) {
                return true;
            }
            unsigned enabled = 0, disabled = 0, id = 0, actions = 0;
            RECT item{};
            if (!read(native_game::ScriptInputRef(object).enabled().data(), enabled) ||
                !read(native_game::ScriptInputRef(object).disabled().data(), disabled) ||
                !read(native_game::ScriptControlRef(object).id().data(), id) ||
                !read(native_game::ScriptControlRef(object, &profile).rectangle().bounds().data(),
                      item) ||
                !read(native_game::ScriptControlRef(object).action_count().data(), actions)) {
                return false;
            }
            if (id == script_control::dialog_background || id == script_control::acknowledgement ||
                id == script_control::dialog_text) {
                return false;
            }
            if (!enabled || disabled || !script_control::selectable(id) || !actions ||
                actions > maximum_native_entries || !rectangle(item) ||
                item.right - item.left > 32 || item.bottom - item.top > 32) {
                return true;
            }
            const bool is_up = PtInRect(&item, up) != FALSE;
            const bool is_down = PtInRect(&item, down) != FALSE;
            up_count += is_up;
            down_count += is_down;
            if (EqualRect(&item, &bounds) && (is_up != is_down)) {
                ++selected_count;
                direction = is_up ? -1 : 1;
            }
            return true;
        });
    return valid && up_count == 1 && down_count == 1 && selected_count == 1;
}

bool owned_text(std::byte* view, std::byte* image, const Edition& profile) {
    const auto owner = native_game::MainViewScriptRef(view).text_owner().data();
    bool shown = false;
    if (!visit<ChildView>(native_game::MainViewRef(view, &profile).children().data(),
                          [&](ChildView* object) {
                              ChildView child{};
                              if (!read(object, child)) {
                                  return false;
                              }
                              shown |= child.object == owner;
                              return true;
                          }) ||
        !shown) {
        return false;
    }
    unsigned count = 0, current = 0;
    std::byte** entries = nullptr;
    std::byte* selected = nullptr;
    const auto group = native_game::NativeRuntime(image, profile).main_text().data();
    if (!read(native_game::ScriptTextOwnerRef(owner).count().data(), count) || !count ||
        count > maximum_native_entries ||
        !read(native_game::ScriptTextOwnerRef(owner).current().data(), current) ||
        current >= count ||
        !read(native_game::ScriptTextOwnerRef(owner).entries().data(), entries) || !entries ||
        !read(entries + current, selected) || selected != group ||
        !type(group, native_game::NativeRuntime(image, profile).main_text_type()) ||
        !type(native_game::MainTextGroupRef(group).rows().data(),
              native_game::NativeRuntime(image, profile).cursor_group_list_type())) {
        return false;
    }
    Rectangle viewport{};
    if (!read(native_game::MainTextGroupRef(group, &profile).viewport().data(), viewport) ||
        viewport.vtable != native_game::NativeRuntime(image, profile).native_rectangle_type() ||
        !rectangle(viewport.bounds)) {
        return false;
    }
    return visit<std::byte>(
        native_game::MainTextGroupRef(group).rows().data(), [&](std::byte* record) {
            native_game::TextRowPrefix row{};
            return read(record, row) &&
                   type(row.text, native_game::NativeRuntime(image, profile).text_type()) &&
                   row.point.vtable ==
                       native_game::NativeRuntime(image, profile).native_point_type();
        });
}

bool owned_photos(std::byte* state, std::byte* view, std::byte* image, const Edition& profile) {
    const auto owner = native_game::MainViewScriptRef(view).photo_owner().data();
    unsigned shown = 0;
    if (!visit<ChildView>(native_game::MainViewRef(view, &profile).children().data(),
                          [&](ChildView* object) {
                              ChildView child{};
                              if (!read(object, child)) {
                                  return false;
                              }
                              shown += child.object == owner;
                              return true;
                          }) ||
        shown != 1 ||
        !type(owner, native_game::NativeRuntime(image, profile).cursor_group_type()) ||
        !type(native_game::InventoryRef(owner).items().data(),
              native_game::NativeRuntime(image, profile).cursor_group_list_type()) ||
        !type(native_game::NativeRuntime(image, profile).photo_gallery().data(),
              native_game::NativeRuntime(image, profile).photo_gallery_type()) ||
        !type(native_game::ScriptStateRef(state).photos().data(),
              native_game::NativeRuntime(image, profile).photo_state_list_type())) {
        return false;
    }
    unsigned index = 0;
    if (!read(native_game::PhotoGalleryRef(
                  native_game::NativeRuntime(image, profile).photo_gallery().data())
                  .first()
                  .data(),
              index) ||
        index % native_game::script_geometry::photo_columns ||
        index > static_cast<unsigned>(std::numeric_limits<int>::max() -
                                      native_game::script_geometry::photo_columns)) {
        return false;
    }
    std::vector<std::byte*> thumbnails;
    if (!visit<std::byte>(
            native_game::NativeRuntime(image, profile).photo_gallery().data(),
            [&](std::byte* object) {
                std::byte* photo = nullptr;
                if (thumbnails.size() == native_game::script_geometry::maximum_thumbnails ||
                    !type(object,
                          native_game::NativeRuntime(image, profile).photo_thumbnail_type()) ||
                    !read(native_game::PhotoThumbnailRef(object).photo().data(), photo) || !photo ||
                    std::find(thumbnails.begin(), thumbnails.end(), object) != thumbnails.end()) {
                    return false;
                }
                thumbnails.push_back(object);
                return true;
            })) {
        return false;
    }
    std::byte* preview = nullptr;
    if (!read(native_game::NativeRuntime(image, profile).photo_preview().data(), preview)) {
        return false;
    }
    if (preview) {
        std::byte* photo = nullptr;
        if (!type(preview, native_game::NativeRuntime(image, profile).photo_thumbnail_type()) ||
            !read(native_game::PhotoThumbnailRef(preview).photo().data(), photo) || !photo ||
            std::find(thumbnails.begin(), thumbnails.end(), preview) != thumbnails.end()) {
            return false;
        }
        thumbnails.push_back(preview);
    }
    std::vector<std::byte*> state_items, graphics;
    if (!visit<std::byte>(native_game::ScriptStateRef(state).photos().data(),
                          [&](std::byte* object) {
                              if (std::find(thumbnails.begin(), thumbnails.end(), object) ==
                                      thumbnails.end() ||
                                  std::find(state_items.begin(), state_items.end(), object) !=
                                      state_items.end()) {
                                  return false;
                              }
                              state_items.push_back(object);
                              return true;
                          }) ||
        state_items.size() != thumbnails.size()) {
        return false;
    }
    if (!visit<InventoryItem>(
            native_game::InventoryRef(owner).items().data(), [&](InventoryItem* object) {
                InventoryItem row{};
                if (!read(object, row) ||
                    row.point_vtable !=
                        native_game::NativeRuntime(image, profile).native_point_type()) {
                    return false;
                }
                const auto graphic = native_game::MemoryRef<InventoryIcon>(row.icon).data();
                const auto found =
                    std::find_if(thumbnails.begin(), thumbnails.end(), [&](std::byte* thumb) {
                        return native_game::PhotoThumbnailRef(thumb).graphic().data() == graphic;
                    });
                native_game::NativePoint point{};
                if (found == thumbnails.end() ||
                    !read(native_game::PhotoThumbnailRef(*found).point().data(), point) ||
                    point.vtable !=
                        native_game::NativeRuntime(image, profile).native_point_type() ||
                    row.position.x != point.value.x || row.position.y != point.value.y ||
                    std::find(graphics.begin(), graphics.end(), graphic) != graphics.end()) {
                    return false;
                }
                if (*found == preview
                        ? (row.position.x != native_game::script_geometry::photo_preview.x ||
                           row.position.y != native_game::script_geometry::photo_preview.y)
                        : (row.position.x < native_game::script_geometry::photo_grid.left ||
                           row.position.x >= native_game::script_geometry::photo_grid.right ||
                           row.position.y < native_game::script_geometry::photo_grid.top ||
                           row.position.y >= native_game::script_geometry::photo_grid.bottom)) {
                    return false;
                }
                graphics.push_back(graphic);
                return true;
            })) {
        return false;
    }
    return graphics.size() == thumbnails.size();
}
}

bool scroll_script_button(void* native_state, void* native_view, std::byte* image,
                          const Edition& profile, unsigned resource, const RECT& bounds,
                          ScrollText up_callback, ScrollText down_callback) {
    if (!native_state || !native_view || !image || !rectangle(bounds)) {
        return false;
    }
    if (!profile.main_scroll_up || !profile.main_scroll_down || !profile.main_text_type ||
        !profile.native_rectangle_type || !profile.native_point_type) {
        return false;
    }
    const auto buttons = native_game::script_geometry::text_arrows(resource);
    if (!buttons) {
        return false;
    }
    const auto state = static_cast<std::byte*>(native_state);
    int direction = 0;
    if (!resources(state, image, profile, resource) ||
        !arrows(state, image, profile, bounds, buttons->up, buttons->down, direction) ||
        !owned_text(static_cast<std::byte*>(native_view), image, profile)) {
        return false;
    }
    const native_game::ScriptRuntime native(image, profile);
    // MainScroll uses these same native callbacks without mouse or held-arrow events.
    native.main_text().scroll(direction, up_callback, down_callback);
    return true;
}

ScrollResult scroll_photo_button(void* native_state, void* native_view, std::byte* image,
                                 const Edition& profile, const RECT& bounds,
                                 ScrollPhotos up_callback, ScrollPhotos down_callback,
                                 ProcessCommands flush_callback) {
    if (photo_dispatching || !native_state || !native_view || !image || !rectangle(bounds)) {
        return ScrollResult::unavailable;
    }
    if (!profile.photo_gallery || !profile.photo_preview || !profile.photo_scroll_up ||
        !profile.photo_scroll_down || !profile.photo_gallery_type ||
        !profile.photo_thumbnail_type || !profile.command_queue_type ||
        !profile.photo_state_list_type || !profile.process_commands || !profile.native_point_type) {
        return ScrollResult::unavailable;
    }
    const auto state = static_cast<std::byte*>(native_state);
    int direction = 0;
    if (!resources(state, image, profile, resource::workstation_photo) ||
        !arrows(state, image, profile, bounds, native_game::script_geometry::photo_arrows.up,
                native_game::script_geometry::photo_arrows.down, direction)) {
        return ScrollResult::unavailable;
    }
    std::byte* app = nullptr;
    void* actual_state = nullptr;
    void* actual_view = nullptr;
    if (!read(native_game::NativeRuntime(image, profile).application().data(), app) || !app ||
        !read(native_game::ApplicationRef(app).state().data(), actual_state) ||
        actual_state != native_state ||
        !read(native_game::ApplicationRef(app).view().data(), actual_view) ||
        actual_view != native_view) {
        return ScrollResult::unavailable;
    }
    const auto idle = [&](std::byte* queue) {
        native_game::CommandQueue command{};
        if (!read(queue, command) ||
            command.data.vtable !=
                native_game::NativeRuntime(image, profile).command_queue_type() ||
            command.data.capacity < minimum_command_capacity ||
            command.data.capacity > maximum_command_capacity || !command.data.storage ||
            command.data.count > command.data.capacity) {
            return ScrollResult::unavailable;
        }
        if (command.data.count || command.data.current != std::numeric_limits<unsigned>::max()) {
            return ScrollResult::pending;
        }
        std::array<void*, minimum_command_capacity> storage{};
        return command.last == std::numeric_limits<unsigned>::max() &&
                       read(command.data.storage, storage)
                   ? ScrollResult::activated
                   : ScrollResult::unavailable;
    };
    const native_game::ApplicationRef queues(app);
    for (const auto queue : {queues.commands(), queues.pointer_events()}) {
        if (const auto status = idle(queue.data()); status != ScrollResult::activated) {
            return status;
        }
    }
    if (!owned_photos(state, static_cast<std::byte*>(native_view), image, profile)) {
        return ScrollResult::unavailable;
    }
    const auto application = native_game::ScriptRuntime(image, profile).application(app);

    struct Dispatch {
        Dispatch() {
            photo_dispatching = true;
        }

        ~Dispatch() {
            photo_dispatching = false;
        }
    } dispatch;

    application.commands().scroll_photos(direction, up_callback, down_callback);
    const auto current = [&] {
        std::byte* candidate = nullptr;
        return read(native_game::NativeRuntime(image, profile).application().data(), candidate) &&
               candidate == app &&
               read(native_game::ApplicationRef(app).state().data(), actual_state) &&
               actual_state == native_state &&
               read(native_game::ApplicationRef(app).view().data(), actual_view) &&
               actual_view == native_view;
    };
    if (!current() || idle(queues.pointer_events().data()) != ScrollResult::activated ||
        !resources(state, image, profile, resource::workstation_photo)) {
        return ScrollResult::unavailable;
    }
    application.process_commands(flush_callback);
    if (!current() || idle(queues.commands().data()) != ScrollResult::activated ||
        idle(queues.pointer_events().data()) != ScrollResult::activated ||
        !owned_photos(state, static_cast<std::byte*>(native_view), image, profile)) {
        return ScrollResult::unavailable;
    }
    return ScrollResult::activated;
}
}
