#include "world_cursors.h"

namespace enhancements::game {
namespace {

template <class T> bool read(const void* source, T& value) {
    SIZE_T bytes = 0;
    return source &&
           ReadProcessMemory(GetCurrentProcess(), source, &value, sizeof(value), &bytes) &&
           bytes == sizeof(value);
}

const void* field(const void* object, unsigned offset) {
    return object ? reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(object) + offset)
                  : nullptr;
}

struct Object {
    const void* type;
    unsigned visible, enabled, blocked, flags, active;
    const void* resource;
    bool operator==(const Object&) const = default;
};

struct Resource {
    const void* type;
    unsigned id;
    bool operator==(const Resource&) const = default;
};

Interaction category(unsigned id, bool shape) {
    switch (id) {
        case 0x1bfc:
            return Interaction::view;
        case 0x122:
            return Interaction::talk;
        case 0x4454:
            return Interaction::use;
        case 0x11c:
            return shape ? Interaction::move_left : Interaction::click;
        case 0x11d:
            return shape ? Interaction::move_right : Interaction::click;
        case 0x11e:
        case 0x120:
        case 0xea2e:
            return shape ? Interaction::move_forward : Interaction::click;
        case 0x11f:
        case 0x117a:
            return shape ? Interaction::move_back : Interaction::click;
        default:
            return Interaction::click;
    }
}

}

Interaction world_cursor_interaction(const void* object, const void* resource, Application*,
                                     std::byte* image, const Edition& profile,
                                     Interaction fallback) {
    if (!image || !resource || fallback != Interaction::click) {
        return fallback;
    }
    Object target{};
    Resource descriptor{};
    if (!read(object, target) || target.resource != resource || !target.enabled || target.blocked ||
        !read(resource, descriptor) || !descriptor.id) {
        return fallback;
    }
    unsigned offset = 0;
    if (profile.world_hotspot_resource && target.type == image + profile.world_hotspot &&
        descriptor.type == image + profile.world_hotspot_resource) {
        offset = 0x40;
    } else if (profile.world_picture_resource && target.type == image + profile.world_picture &&
               descriptor.type == image + profile.world_picture_resource) {
        offset = 0x44;
    } else {
        return fallback;
    }
    unsigned id = 0;
    if (!read(field(resource, offset), id)) {
        return fallback;
    }
    // Native CharTeleport can replace a picture's configured movement asset.
    const auto result = category(id, offset == 0x40);
    Object target_after{};
    Resource descriptor_after{};
    unsigned id_after = 0;
    return read(object, target_after) && target_after == target &&
                   read(resource, descriptor_after) && descriptor_after == descriptor &&
                   read(field(resource, offset), id_after) && id_after == id
               ? result
               : fallback;
}

Interaction navigation_cursor_interaction(const void* object, const void* wrapper,
                                          const void* shape, std::byte* image,
                                          const Edition& profile) {
    Object target{};
    Resource own{}, linked{};
    unsigned link = 0, cursor = 0;
    if (!image || !profile.world_navigation || !profile.world_navigation_resource ||
        !profile.world_navigation_shape_resource || !read(object, target) ||
        target.type != image + profile.world_navigation || !target.enabled || target.blocked ||
        target.resource != wrapper || !read(wrapper, own) ||
        own.type != image + profile.world_navigation_resource || !own.id ||
        !read(field(wrapper, 0x2c), link) || !link || !read(shape, linked) ||
        linked.type != image + profile.world_navigation_shape_resource || linked.id != link ||
        !read(field(shape, 0x40), cursor)) {
        return Interaction::click;
    }
    Object target_after{};
    Resource own_after{}, linked_after{};
    unsigned link_after = 0, cursor_after = 0;
    // The wrapper link names the owned class39 shape, not the wrapper itself.
    return read(object, target_after) && target_after == target && read(wrapper, own_after) &&
                   own_after == own && read(shape, linked_after) && linked_after == linked &&
                   read(field(wrapper, 0x2c), link_after) && link_after == link &&
                   read(field(shape, 0x40), cursor_after) && cursor_after == cursor
               ? category(cursor, true)
               : Interaction::click;
}

}
