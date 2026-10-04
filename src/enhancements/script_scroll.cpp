#include "script_scroll.h"
#include "game_resources.h"

#include <array>

namespace enhancements::game {
namespace {
struct Binding {
    unsigned text, up, down, type, rows, rectangle, point;
};

constexpr std::array bindings{
    Binding{0x2bd3f8, 0x150c40, 0x150da0, 0x25d87c, 0x25bf00, 0x25bc38, 0x25c1b0},
    Binding{0x2c0560, 0x153c60, 0x153dc0, 0x260014, 0x25e698, 0x25e3d0, 0x25e948},
    Binding{0x2c15c0, 0x154200, 0x154360, 0x261014, 0x25f800, 0x25f3d0, 0x25f948},
    Binding{0x2c2690, 0x13a9d0, 0x13ab30, 0x25f7b4, 0x25f400, 0x25f540, 0x25f258}};

template <class T> bool read(const void* address, T& value) {
    SIZE_T copied = 0;
    return address &&
           ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &copied) &&
           copied == sizeof(value);
}

template <class T, class F> bool visit(const void* address, F callback) {
    List<T> list{};
    if (!read(address, list) || list.count > 256) {
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

bool type(const void* object, std::byte* expected) {
    void* actual = nullptr;
    return read(object, actual) && actual == expected;
}

bool rectangle(const RECT& bounds) {
    return bounds.left >= 0 && bounds.top >= 0 && bounds.right <= 640 && bounds.bottom <= 480 &&
           bounds.right > bounds.left && bounds.bottom > bounds.top;
}

bool resources(std::byte* state, std::byte* image, const Edition& profile, unsigned wanted) {
    bool found = false, blocked = false;
    const bool valid = visit<std::byte>(state + 0x25c, [&](std::byte* object) {
        if (!type(object, image + profile.script_root)) {
            return true;
        }
        std::byte* resource = nullptr;
        unsigned id = 0;
        if (!read(object + 0x18, resource) || !resource || !read(resource + 4, id)) {
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
    const bool valid = visit<std::byte>(state + 0x270, [&](std::byte* object) {
        if (!type(object, image + profile.script_control)) {
            return true;
        }
        unsigned enabled = 0, disabled = 0, id = 0, actions = 0;
        RECT item{};
        if (!read(object + 8, enabled) || !read(object + 12, disabled) ||
            !read(object + 0x144, id) || !read(object + profile.control_rectangle + 4, item) ||
            !read(object + 0x20, actions)) {
            return false;
        }
        if (id == script_control::dialog_background || id == script_control::acknowledgement ||
            id == script_control::dialog_text) {
            return false;
        }
        if (!enabled || disabled || !script_control::selectable(id) || !actions || actions > 256 ||
            !rectangle(item) || item.right - item.left > 32 || item.bottom - item.top > 32) {
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

bool owned_text(std::byte* view, std::byte* image, const Edition& profile, const Binding& binding) {
    const auto owner = view + 0x4c8;
    bool shown = false;
    if (!visit<ChildView>(view + profile.children,
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
    const auto group = image + profile.main_text;
    if (!read(owner + 8, count) || !count || count > 256 || !read(owner + 12, current) ||
        current >= count || !read(owner + 16, entries) || !entries ||
        !read(entries + current, selected) || selected != group ||
        !type(group, image + binding.type) || !type(group + 4, image + binding.rows)) {
        return false;
    }
    Rectangle viewport{};
    if (!read(group + profile.main_text_rectangle, viewport) ||
        viewport.vtable != image + binding.rectangle || !rectangle(viewport.bounds)) {
        return false;
    }
    return visit<std::byte>(group + 4, [&](std::byte* record) {
        std::array<std::byte*, 5> row{};
        return read(record, row) && type(row[0], image + profile.text) &&
               row[1] == image + binding.point;
    });
}
}

bool scroll_script_button(void* native_state, void* native_view, std::byte* image,
                          const Edition& profile, unsigned resource, const RECT& bounds,
                          ScrollText up_callback, ScrollText down_callback) {
    if (!native_state || !native_view || !image || !rectangle(bounds)) {
        return false;
    }
    const auto binding = std::find_if(bindings.begin(), bindings.end(), [&](const Binding& item) {
        return item.text == profile.main_text;
    });
    if (binding == bindings.end()) {
        return false;
    }
    POINT up{}, down{};
    switch (resource) {
        case resource::pda_notes:
            up = {422, 95};
            down = {422, 339};
            break;
        case resource::pda_message:
            up = {428, 62};
            down = {428, 351};
            break;
        case resource::workstation_message:
            up = {607, 107};
            down = {607, 443};
            break;
        default:
            return false;
    }
    const auto state = static_cast<std::byte*>(native_state);
    int direction = 0;
    if (!resources(state, image, profile, resource) ||
        !arrows(state, image, profile, bounds, up, down, direction) ||
        !owned_text(static_cast<std::byte*>(native_view), image, profile, *binding)) {
        return false;
    }
    const auto callback =
        direction < 0
            ? (up_callback ? up_callback : reinterpret_cast<ScrollText>(image + binding->up))
            : (down_callback ? down_callback : reinterpret_cast<ScrollText>(image + binding->down));
    // MainScroll uses these same native callbacks without mouse or held-arrow events.
    callback(reinterpret_cast<ChoiceList*>(image + profile.main_text));
    return true;
}
}
