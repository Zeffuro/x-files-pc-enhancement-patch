#include "input_source.h"
#include "inventory.h"

#include <algorithm>

namespace enhancements {
namespace {

thread_local bool focused = false;
thread_local POINT return_point{};

bool move_cursor(HWND window, POINT point) {
    return ClientToScreen(window, &point) && move_controller_pointer(point.x, point.y);
}

bool point_at(HWND window, const RECT& bounds) {
    return move_cursor(window,
                       {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2});
}

}

std::vector<RECT> inventory_bounds(const game::MainView* view) {
    constexpr unsigned max_items = 64;
    if (!view) {
        return {};
    }
    const auto& children = *reinterpret_cast<const game::List<game::ChildView>*>(
        reinterpret_cast<const std::byte*>(view) + game::edition().children);
    if (view->inventory.items.count > max_items || children.count > max_items) {
        return {};
    }
    bool visible = false;
    auto child = children.first;
    for (unsigned index = 0; child && index < children.count; ++index, child = child->next) {
        visible |= child->value && child->value->object == &view->inventory;
    }
    if (!visible) {
        return {};
    }
    std::vector<RECT> result;
    auto node = view->inventory.items.first;
    for (unsigned index = 0; node && index < view->inventory.items.count;
         ++index, node = node->next) {
        if (!node->value || !node->value->icon) {
            continue;
        }
        const auto bounds = node->value->icon->rectangle.bounds;
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
    auto node = view->inventory.items.first;
    for (unsigned i = 0; node && i < view->inventory.items.count; ++i, node = node->next) {
        const auto icon = node->value ? node->value->icon : nullptr;
        if (icon && icon->graphic && icon->graphic->resource &&
            icon->graphic->resource->id == resource &&
            std::any_of(visible.begin(), visible.end(), [&](const RECT& bounds) {
                return EqualRect(&bounds, &icon->rectangle.bounds);
            })) {
            return icon->rectangle.bounds;
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

bool focus_inventory(HWND window) {
    if (inventory_focused(window)) {
        return leave_inventory(window);
    }
    const auto items = inventory_bounds(game::current_view());
    if (items.empty() || !GetCursorPos(&return_point) || !ScreenToClient(window, &return_point)) {
        return false;
    }
    focused = point_at(window, items.front());
    return focused;
}

bool leave_inventory(HWND window) {
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
}

}
