#include "menu_corner.h"
#include "game_ui.h"
#include <limits>

namespace enhancements::game {
namespace {

bool readable(const void* address, SIZE_T size) {
    auto cursor = reinterpret_cast<std::uintptr_t>(address);
    if (!cursor || !size || size - 1 > std::numeric_limits<std::uintptr_t>::max() - cursor) {
        return false;
    }
    while (size) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) ||
            region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD)) {
            return false;
        }
        switch (region.Protect & 0xff) {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                break;
            default:
                return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (cursor < base || cursor - base >= region.RegionSize) {
            return false;
        }
        const auto available = region.RegionSize - (cursor - base);
        if (size <= available) {
            return true;
        }
        size -= available;
        cursor += available;
    }
    return false;
}

template <class T> bool read(const void* address, T& value) {
    SIZE_T bytes = 0;
    return readable(address, sizeof(value)) &&
           ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &bytes) &&
           bytes == sizeof(value);
}

bool write(RECT* address, const RECT& value) {
    SIZE_T bytes = 0;
    return WriteProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &bytes) &&
           bytes == sizeof(value);
}

bool valid(const RECT& bounds) {
    return bounds.left >= 0 && bounds.top >= 0 && bounds.left < bounds.right &&
           bounds.top < bounds.bottom && bounds.right <= 640 && bounds.bottom <= 480;
}

struct Object {
    void* vtable;
    unsigned visible, enabled, disabled;
};

struct Snapshot {
    Application* application = nullptr;
    void* state = nullptr;
    MainView* view = nullptr;
    std::byte* object = nullptr;
    Rectangle* rectangle = nullptr;
    Rectangle value{};
};

struct Suppression {
    Snapshot original{};
    bool active = false;
} suppression;

bool inspect(Snapshot& result) {
    const auto image = executable_image();
    const auto& profile = edition();
    Application app{};
    List<std::byte> list{};
    if (!image || !read(image + profile.application, result.application) ||
        !read(result.application, app) || !app.state || !app.view ||
        !read(static_cast<std::byte*>(app.state) + 0x20c, list) || list.count > 256 ||
        (!list.count && (list.first || list.last))) {
        return false;
    }
    result.state = app.state;
    result.view = app.view;
    auto node = list.first;
    List<std::byte>::Node* previous = nullptr;
    unsigned matches = 0;
    for (unsigned i = 0; i < list.count; ++i) {
        List<std::byte>::Node item{};
        Object object{};
        if (!read(node, item) || !item.value || item.previous != previous ||
            !read(item.value, object) || !object.vtable) {
            return false;
        }
        if (object.vtable == image + profile.menu_corner) {
            if (++matches != 1 || !object.enabled || object.disabled) {
                return false;
            }
            result.object = item.value;
        }
        previous = node;
        node = item.next;
    }
    result.rectangle = reinterpret_cast<Rectangle*>(image + profile.menu_corner_rectangle);
    return !node && previous == list.last && matches == 1 && read(result.rectangle, result.value) &&
           result.value.vtable;
}

bool owns(const Snapshot& current) {
    const auto& original = suppression.original;
    const RECT empty{};
    return suppression.active && original.application == current.application &&
           original.state == current.state && original.view == current.view &&
           original.object == current.object && original.rectangle == current.rectangle &&
           original.value.vtable == current.value.vtable &&
           EqualRect(&current.value.bounds, &empty);
}

bool restore() {
    if (!suppression.active) {
        return true;
    }
    Rectangle current{};
    const auto& original = suppression.original;
    const RECT empty{};
    // Restore only the bounds still owned by this suppression.
    const bool owned = read(original.rectangle, current) &&
                       original.value.vtable == current.vtable &&
                       EqualRect(&current.bounds, &empty);
    const bool restored = owned && write(&original.rectangle->bounds, original.value.bounds);
    suppression.active = owned && !restored;
    return restored;
}

}

std::optional<RECT> menu_corner() {
    Snapshot current;
    const bool eligible = inspect(current);
    if (suppression.active) {
        if (eligible && owns(current)) {
            return suppression.original.value.bounds;
        }
        restore();
        if (!eligible || !inspect(current)) {
            return std::nullopt;
        }
    }
    return eligible && valid(current.value.bounds) ? std::optional<RECT>(current.value.bounds)
                                                   : std::nullopt;
}

bool suppress_menu_corner(bool suppress) {
    if (!suppress) {
        return restore();
    }
    Snapshot current;
    if (!inspect(current)) {
        restore();
        return false;
    }
    if (suppression.active) {
        if (owns(current)) {
            return true;
        }
        restore();
        if (!inspect(current)) {
            return false;
        }
    }
    if (!valid(current.value.bounds) || !write(&current.rectangle->bounds, RECT{})) {
        return false;
    }
    suppression.original = current;
    suppression.active = true;
    return true;
}

std::optional<MenuCornerIdentity> menu_corner_identity() {
    Snapshot current;
    if (!inspect(current) || (!valid(current.value.bounds) && !owns(current))) {
        return std::nullopt;
    }
    return MenuCornerIdentity{current.application, current.state, current.view, current.object};
}

}
