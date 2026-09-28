#include "input_source.h"
#include "inventory.h"
#include "game/layouts/asset_reference.h"

#include <algorithm>

namespace enhancements {
namespace {

thread_local bool focused = false;
thread_local POINT return_point{};
thread_local game::MainView* revealing = nullptr;
thread_local POINT reveal_pointer{};
thread_local ULONGLONG reveal_deadline = 0;

template <class T> bool read(const void* address, T& value) {
    SIZE_T bytes = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &bytes) &&
           bytes == sizeof(value);
}

template <class T> const void* field(const T* object, std::size_t offset) {
    return reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(object) + offset);
}

bool visible_inventory(const game::MainView* view) {
    game::List<game::ChildView> children{};
    if (!view || !read(field(view, game::edition().children), children) || children.count > 64) {
        return false;
    }
    auto* address = children.first;
    for (unsigned i = 0; address && i < children.count; ++i) {
        game::List<game::ChildView>::Node node{};
        game::ChildView child{};
        if (!read(address, node)) {
            break;
        }
        if (read(node.value, child) &&
            child.object == field(view, offsetof(game::MainView, inventory))) {
            return true;
        }
        address = node.next;
    }
    return false;
}

bool move_cursor(HWND window, POINT point) {
    return ClientToScreen(window, &point) && move_controller_pointer(point.x, point.y);
}

bool point_at(HWND window, const RECT& bounds) {
    return move_cursor(window,
                       {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2});
}

}

std::vector<InventoryEntry> inventory_items(const game::MainView* view) {
    game::Inventory inventory{};
    if (!view || !read(field(view, offsetof(game::MainView, inventory)), inventory) ||
        inventory.items.count > 256) {
        return {};
    }
    std::vector<InventoryEntry> result;
    auto* address = inventory.items.first;
    for (unsigned i = 0; address && i < inventory.items.count; ++i) {
        game::List<game::InventoryItem>::Node node{};
        game::InventoryItem item{};
        game::InventoryIcon icon{};
        if (!read(address, node)) {
            break;
        }
        address = node.next;
        if (!read(node.value, item) || !read(item.icon, icon)) {
            continue;
        }
        InventoryEntry entry{icon.rectangle.bounds, std::nullopt};
        game::InventoryIcon::Graphic graphic{};
        game::InventoryIcon::Graphic::Resource resource{};
        // MoviesTask can dispatch this inspection while native objects are changing.
        if (read(icon.graphic, graphic) && read(graphic.resource, resource)) {
            entry.resource = resource.id;
            entry.asset_path = native_game::read_asset_path(
                graphic.resource, game::executable_image(), game::edition());
        }
        result.push_back(entry);
    }
    return result;
}

std::vector<RECT> inventory_bounds(const game::MainView* view) {
    if (!visible_inventory(view)) {
        return {};
    }
    const auto items = inventory_items(view);
    if (items.size() > 64) {
        return {};
    }
    std::vector<RECT> result;
    for (const auto& item : items) {
        const auto& bounds = item.bounds;
        if (bounds.left >= 0 && bounds.top >= 0 && bounds.right <= 640 && bounds.bottom <= 480 &&
            bounds.right > bounds.left && bounds.bottom > bounds.top) {
            result.push_back(bounds);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const RECT& a, const RECT& b) { return a.left < b.left; });
    return result;
}

bool inventory_focused(HWND window) {
    if (!focused) {
        return false;
    }
    const auto items = inventory_bounds(game::current_view());
    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(window, &cursor) ||
        std::none_of(items.begin(), items.end(),
                     [&](const RECT& item) { return PtInRect(&item, cursor) != FALSE; })) {
        focused = false;
    }
    return focused;
}

std::optional<RECT> inventory_item_bounds(const game::MainView* view, unsigned resource) {
    const auto visible = inventory_bounds(view);
    if (visible.empty()) {
        return std::nullopt;
    }
    for (const auto& item : inventory_items(view)) {
        if (item.resource == resource &&
            std::any_of(visible.begin(), visible.end(),
                        [&](const RECT& bounds) { return EqualRect(&bounds, &item.bounds); })) {
            return item.bounds;
        }
    }
    return std::nullopt;
}

bool focus_inventory_item(HWND window, unsigned resource) {
    const auto item = inventory_item_bounds(game::current_view(), resource);
    const auto scene = game::scene_bounds();
    if (!item || IsRectEmpty(&scene) || !GetCursorPos(&return_point) ||
        !ScreenToClient(window, &return_point)) {
        return false;
    }
    if (!PtInRect(&scene, return_point)) {
        return_point = {(scene.left + scene.right) / 2, (scene.top + scene.bottom) / 2};
    }
    focused = point_at(window, *item);
    return focused;
}

bool reveal_inventory(HWND window) {
    const auto* view = game::current_view();
    if (revealing || !view || visible_inventory(view) || !game::world_navigation_available()) {
        return false;
    }
    if (!GetCursorPos(&return_point) || !ScreenToClient(window, &return_point)) {
        return false;
    }
    if (!move_cursor(window, {20, 440}) || !GetCursorPos(&reveal_pointer)) {
        return false;
    }
    // Native pointer movement into the bottom band populates hidden inventory.
    revealing = game::current_view();
    reveal_deadline = GetTickCount64() + 750;
    return revealing == view;
}

bool inventory_reveal_pending() {
    return revealing != nullptr;
}

bool focus_inventory(HWND window) {
    if (revealing || inventory_focused(window)) {
        return leave_inventory(window);
    }
    const auto items = inventory_bounds(game::current_view());
    if (items.empty()) {
        return reveal_inventory(window);
    }
    if (!GetCursorPos(&return_point) || !ScreenToClient(window, &return_point)) {
        return false;
    }
    focused = point_at(window, items.front());
    return focused;
}

void update_inventory_focus(HWND window, bool enabled) {
    if (!revealing) {
        return;
    }
    POINT cursor{};
    if (!enabled || revealing != game::current_view() || !game::world_navigation_available() ||
        GetTickCount64() >= reveal_deadline || !GetCursorPos(&cursor) ||
        cursor.x != reveal_pointer.x || cursor.y != reveal_pointer.y) {
        revealing = nullptr;
        return;
    }
    const auto items = inventory_bounds(revealing);
    if (!items.empty()) {
        revealing = nullptr;
        focused = point_at(window, items.front());
    }
}

bool leave_inventory(HWND window) {
    if (revealing) {
        revealing = nullptr;
        move_cursor(window, return_point);
        return true;
    }
    if (!inventory_focused(window)) {
        return false;
    }
    focused = false;
    move_cursor(window, return_point);
    return true;
}

bool navigate_inventory(HWND window, int direction) {
    if (!inventory_focused(window)) {
        return false;
    }
    const auto items = inventory_bounds(game::current_view());
    POINT cursor{};
    if (!direction || !GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) {
        return true;
    }
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (PtInRect(&items[index], cursor)) {
            const auto count = static_cast<int>(items.size());
            const auto next = (static_cast<int>(index) + direction + count) % count;
            point_at(window, items[next]);
            break;
        }
    }
    return true;
}

void clear_inventory_focus() {
    focused = false;
    revealing = nullptr;
}

}
