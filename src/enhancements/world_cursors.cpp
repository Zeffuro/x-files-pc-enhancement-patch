#include "game/functions/runtime.h"
#include "world_cursors.h"
#include "game/layouts/native_objects.h"
#include "game/layouts/database/story.h"
#include "game/resources.h"
#include "game/access/world.h"
#include "game/access/database.h"

namespace enhancements::game {
namespace {

template <class T> bool read(const void* source, T& value) {
    SIZE_T bytes = 0;
    return source &&
           ReadProcessMemory(GetCurrentProcess(), source, &value, sizeof(value), &bytes) &&
           bytes == sizeof(value);
}

Interaction category(unsigned id, bool shape) {
    switch (id) {
        case native_game::cursor_id::view:
            return Interaction::view;
        case native_game::cursor_id::talk:
            return Interaction::talk;
        case native_game::cursor_id::use:
            return Interaction::use;
        case native_game::cursor_id::left:
            return shape ? Interaction::move_left : Interaction::click;
        case native_game::cursor_id::right:
            return shape ? Interaction::move_right : Interaction::click;
        case native_game::cursor_id::forward:
        case native_game::cursor_id::forward_alternate:
        case native_game::cursor_id::forward_alternate_2:
            return shape ? Interaction::move_forward : Interaction::click;
        case native_game::cursor_id::back:
        case native_game::cursor_id::back_alternate:
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
    native_game::ResourceObject target{};
    native_game::ResourceHeader descriptor{};
    if (!read(object, target) || target.resource != resource || !target.object.enabled ||
        target.object.blocked || !read(resource, descriptor) || !descriptor.id) {
        return fallback;
    }
    native_game::MemoryRef<unsigned> cursor_field;
    bool shape_cursor = false;
    if (profile.world_hotspot_resource &&
        target.object.type == native_game::NativeRuntime(image, profile).world_hotspot_type() &&
        descriptor.type ==
            native_game::NativeRuntime(image, profile).world_hotspot_resource_type()) {
        cursor_field = native_game::StoryHotspotRef(resource).shape_value();
        shape_cursor = true;
    } else if (profile.world_picture_resource &&
               target.object.type ==
                   native_game::NativeRuntime(image, profile).world_picture_type() &&
               descriptor.type ==
                   native_game::NativeRuntime(image, profile).world_picture_resource_type()) {
        cursor_field = native_game::PictureResourceRef(resource).cursor();
    } else {
        return fallback;
    }
    unsigned id = 0;
    if (!read(cursor_field.data(), id)) {
        return fallback;
    }
    // Native CharTeleport can replace a picture's configured movement asset.
    const auto result = category(id, shape_cursor);
    native_game::ResourceObject target_after{};
    native_game::ResourceHeader descriptor_after{};
    unsigned id_after = 0;
    return read(object, target_after) && target_after == target &&
                   read(resource, descriptor_after) && descriptor_after == descriptor &&
                   read(cursor_field.data(), id_after) && id_after == id
               ? result
               : fallback;
}

Interaction navigation_cursor_interaction(const void* object, const void* wrapper,
                                          const void* shape, std::byte* image,
                                          const Edition& profile) {
    native_game::ResourceObject target{};
    native_game::ResourceHeader own{}, linked{};
    unsigned link = 0, cursor = 0;
    if (!image || !profile.world_navigation || !profile.world_navigation_resource ||
        !profile.world_navigation_shape_resource || !read(object, target) ||
        target.object.type != native_game::NativeRuntime(image, profile).world_navigation_type() ||
        !target.object.enabled || target.object.blocked || target.resource != wrapper ||
        !read(wrapper, own) ||
        own.type != native_game::NativeRuntime(image, profile).world_navigation_resource_type() ||
        !own.id || !read(native_game::NavigationResourceRef(wrapper).shape().data(), link) ||
        !link || !read(shape, linked) ||
        linked.type !=
            native_game::NativeRuntime(image, profile).world_navigation_shape_resource_type() ||
        linked.id != link ||
        !read(native_game::StoryHotspotRef(shape).shape_value().data(), cursor)) {
        return Interaction::click;
    }
    native_game::ResourceObject target_after{};
    native_game::ResourceHeader own_after{}, linked_after{};
    unsigned link_after = 0, cursor_after = 0;
    // The wrapper link names the owned class39 shape, not the wrapper itself.
    return read(object, target_after) && target_after == target && read(wrapper, own_after) &&
                   own_after == own && read(shape, linked_after) && linked_after == linked &&
                   read(native_game::NavigationResourceRef(wrapper).shape().data(), link_after) &&
                   link_after == link &&
                   read(native_game::StoryHotspotRef(shape).shape_value().data(), cursor_after) &&
                   cursor_after == cursor
               ? category(cursor, true)
               : Interaction::click;
}

}
