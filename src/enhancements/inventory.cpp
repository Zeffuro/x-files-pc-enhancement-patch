#include "input_source.h"
#include "inventory.h"
#include "game/layouts/database/asset_reference.h"
#include "game/access/ui.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace enhancements {
namespace {

thread_local bool focused = false;
thread_local const game::MainView* focused_view = nullptr;
thread_local POINT return_point{};
thread_local game::MainView* revealing = nullptr;
thread_local POINT reveal_pointer{};
thread_local ULONGLONG reveal_deadline = 0;

template <class T> bool read(const void* address, T& value) {
    SIZE_T bytes = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &bytes) &&
           bytes == sizeof(value);
}

std::vector<const void*> visible_groups(const game::MainView* view) {
    game::List<game::ChildView> children{};
    const native_game::MainViewRef owner(view, &game::edition());
    if (!view || !read(owner.children().data(), children) || children.count > 64) {
        return {};
    }
    std::vector<const void*> result, visited;
    auto* address = children.first;
    for (unsigned i = 0; address && i < children.count; ++i) {
        game::List<game::ChildView>::Node node{};
        game::ChildView child{};
        if (std::find(visited.begin(), visited.end(), address) != visited.end() ||
            !read(address, node) || !read(node.value, child)) {
            return {};
        }
        visited.push_back(address);
        result.push_back(child.object);
        address = node.next;
    }
    game::List<game::ChildView> current{};
    if (address || result.size() != children.count || !read(owner.children().data(), current) ||
        std::memcmp(&current, &children, sizeof(current))) {
        return {};
    }
    return result;
}

bool visible_inventory(const game::MainView* view) {
    const auto groups = visible_groups(view);
    return view && std::find(groups.begin(), groups.end(),
                             native_game::MainViewRef(view).inventory().data()) != groups.end();
}

bool move_cursor(HWND window, POINT point) {
    return ClientToScreen(window, &point) && move_controller_pointer(point.x, point.y);
}

bool point_at(HWND window, const RECT& bounds) {
    return move_cursor(window,
                       {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2});
}

bool focus_item(HWND window, const game::MainView* view, const RECT& bounds) {
    focused =
        game::current_view() == view && point_at(window, bounds) && game::current_view() == view;
    focused_view = focused ? view : nullptr;
    return focused;
}

std::vector<InventoryEntry> collect_inventory(const game::MainView* view, bool details) {
    game::Inventory inventory{};
    if (!view || !read(native_game::MainViewRef(view).inventory().data(), inventory) ||
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
        if (details && read(icon.graphic, graphic) && read(graphic.resource, resource)) {
            entry.resource = resource.id;
            entry.asset_path = native_game::read_asset_path(
                graphic.resource, game::executable_image(), game::edition());
        }
        result.push_back(entry);
    }
    return result;
}

std::vector<RECT> visible_bounds(const game::MainView* view, std::span<const void* const> owners) {
    const auto groups = visible_groups(view);
    std::vector<RECT> result;
    for (const auto owner : owners) {
        game::Inventory group{};
        if (std::find(groups.begin(), groups.end(), owner) == groups.end() || !read(owner, group) ||
            group.items.count > 64) {
            continue;
        }
        std::vector<RECT> bounds;
        std::vector<const void*> visited;
        auto* address = group.items.first;
        for (unsigned i = 0; address && i < group.items.count; ++i) {
            game::List<game::InventoryItem>::Node node{};
            game::InventoryItem item{};
            game::InventoryIcon icon{};
            if (std::find(visited.begin(), visited.end(), address) != visited.end() ||
                !read(address, node)) {
                break;
            }
            visited.push_back(address);
            address = node.next;
            if (!read(node.value, item) || !read(item.icon, icon)) {
                continue;
            }
            const auto& r = icon.rectangle.bounds;
            if (r.left >= 0 && r.top >= 0 && r.right <= 640 && r.bottom <= 480 &&
                r.right > r.left && r.bottom > r.top) {
                bounds.push_back(r);
            }
        }
        game::Inventory current{};
        if (address || visited.size() != group.items.count || !read(owner, current) ||
            std::memcmp(&current, &group, sizeof(current))) {
            continue;
        }
        result.insert(result.end(), bounds.begin(), bounds.end());
    }
    if (visible_groups(view) != groups) {
        return {};
    }
    std::sort(result.begin(), result.end(), [](const RECT& a, const RECT& b) {
        return a.top == b.top ? a.left < b.left : a.top < b.top;
    });
    result.erase(std::unique(result.begin(), result.end(),
                             [](const RECT& a, const RECT& b) { return EqualRect(&a, &b); }),
                 result.end());
    return result;
}
}

std::vector<InventoryEntry> inventory_items(const game::MainView* view) {
    return collect_inventory(view, true);
}

std::vector<RECT> inventory_bounds(const game::MainView* view) {
    const native_game::MainViewRef owner(view);
    const std::array<const void*, 2> groups{owner.inventory().data(), owner.evidence().data()};
    return view ? visible_bounds(view, groups) : std::vector<RECT>{};
}

std::vector<RECT> evidence_bounds(const game::MainView* view) {
    const native_game::MainViewRef owner(view);
    const std::array<const void*, 4> groups{
        owner.conversation_icons(0).data(), owner.conversation_icons(1).data(),
        owner.conversation_icons(2).data(), owner.evidence().data()};
    return view ? visible_bounds(view, groups) : std::vector<RECT>{};
}

int inventory_target(std::span<const RECT> items, POINT cursor, int horizontal, int vertical) {
    if (items.empty() || (!horizontal && !vertical)) {
        return -1;
    }
    int current = -1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (PtInRect(&items[i], cursor)) {
            current = static_cast<int>(i);
            cursor = {(items[i].left + items[i].right) / 2, (items[i].top + items[i].bottom) / 2};
            break;
        }
    }
    int next = current;
    long long best = std::numeric_limits<long long>::max();
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (static_cast<int>(i) == current) {
            continue;
        }
        const auto& item = items[i];
        const long long dx = (item.left + item.right) / 2 - cursor.x;
        const long long dy = (item.top + item.bottom) / 2 - cursor.y;
        const bool same_row =
            current < 0 || (item.top < items[current].bottom && item.bottom > items[current].top);
        if ((horizontal && (!same_row || dx * horizontal <= 0)) ||
            (!horizontal && (dy * vertical <= 0 || (current >= 0 && same_row)))) {
            continue;
        }
        const auto score = dx * dx + dy * dy;
        if (score < best) {
            best = score;
            next = static_cast<int>(i);
        }
    }
    if (horizontal && next == current && current >= 0) {
        long edge =
            horizontal > 0 ? std::numeric_limits<long>::max() : std::numeric_limits<long>::min();
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            if (item.top >= items[current].bottom || item.bottom <= items[current].top) {
                continue;
            }
            const auto x = (item.left + item.right) / 2;
            if ((horizontal > 0 && x < edge) || (horizontal < 0 && x > edge)) {
                edge = x;
                next = static_cast<int>(i);
            }
        }
    }
    return next;
}

bool inventory_focused(HWND window) {
    if (!focused || focused_view != game::current_view()) {
        focused = false;
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
    const std::array<const void*, 1> groups{native_game::MainViewRef(view).inventory().data()};
    const auto primary_bounds = view ? visible_bounds(view, groups) : std::vector<RECT>{};
    if (primary_bounds.empty()) {
        return std::nullopt;
    }
    for (const auto& item : inventory_items(view)) {
        if (item.resource == resource &&
            std::any_of(primary_bounds.begin(), primary_bounds.end(),
                        [&](const RECT& bounds) { return EqualRect(&bounds, &item.bounds); })) {
            return item.bounds;
        }
    }
    return std::nullopt;
}

bool focus_inventory_item(HWND window, unsigned resource) {
    const auto view = game::current_view();
    const auto item = inventory_item_bounds(view, resource);
    const auto scene = game::scene_bounds();
    if (!item || IsRectEmpty(&scene) || !GetCursorPos(&return_point) ||
        !ScreenToClient(window, &return_point)) {
        return false;
    }
    if (!PtInRect(&scene, return_point)) {
        return_point = {(scene.left + scene.right) / 2, (scene.top + scene.bottom) / 2};
    }
    return focus_item(window, view, *item);
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
    const auto view = game::current_view();
    const auto items = inventory_bounds(view);
    if (items.empty()) {
        return reveal_inventory(window);
    }
    if (!GetCursorPos(&return_point) || !ScreenToClient(window, &return_point)) {
        return false;
    }
    const auto first =
        std::min_element(items.begin(), items.end(), [](const RECT& a, const RECT& b) {
            return a.top == b.top ? a.left < b.left : a.top > b.top;
        });
    return focus_item(window, view, *first);
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
        const auto view = revealing;
        revealing = nullptr;
        const auto first =
            std::min_element(items.begin(), items.end(), [](const RECT& a, const RECT& b) {
                return a.top == b.top ? a.left < b.left : a.top > b.top;
            });
        focus_item(window, view, *first);
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

bool navigate_inventory(HWND window, int horizontal, int vertical) {
    if (!inventory_focused(window)) {
        return false;
    }
    const auto view = game::current_view();
    const auto items = inventory_bounds(view);
    POINT cursor{};
    if ((!horizontal && !vertical) || !GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) {
        return true;
    }
    const auto next = inventory_target(items, cursor, horizontal, vertical);
    if (next >= 0 && game::current_view() == view) {
        point_at(window, items[next]);
    }
    return true;
}

void clear_inventory_focus() {
    focused = false;
    revealing = nullptr;
}

}
