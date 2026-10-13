#pragma once
#include "ui.h"

namespace native_game {

struct ObjectFlags {
    const void* type;
    unsigned visible, enabled, blocked;
    bool operator==(const ObjectFlags&) const = default;
};

struct ResourceObject {
    ObjectFlags object;
    unsigned flags, active;
    const void* resource;
    bool operator==(const ResourceObject&) const = default;
};

struct ResourceHeader {
    const void* type;
    unsigned id;
    bool operator==(const ResourceHeader&) const = default;
};

struct PictureActions {
    const void* slots[6];
    unsigned eligible;
};

struct RuntimeActionExpression {
    std::uint32_t left, right;
    std::uint8_t left_kind, right_kind, operation;
    std::byte reserved;
};

struct SceneStatePrefix {
    std::byte before_navigation[0x68];
    List<void> navigation, pictures;
    std::byte before_emotions[0x130 - 0x90];
    List<void> emotions, owned_inventory;
    std::byte before_hotspots[0x194 - 0x158];
    List<void> hotspots, selection_first;
    std::byte before_selection_second[0x1e4 - 0x1bc];
    List<void> selection_second;
    std::byte before_modal[0x20c - 0x1f8];
    List<void> modal;
    std::byte before_aiming[0x248 - 0x220];
    List<void> aiming_targets;
};

struct ModalViewPrefix {
    std::byte before_background[0x59c];
    Container background;
    std::byte before_foreground[0x5d8 - 0x5a8];
    List<InventoryItem> foreground;
};

struct OwnedInventoryItemPrefix {
    ResourceObject object;
    std::byte before_graphic[0x1e4 - sizeof(ResourceObject)];
    InventoryIcon::Graphic* graphic;
};

struct WorldPicturePrefix {
    ResourceObject object;
    std::byte before_actions[0x148 - sizeof(ResourceObject)];
    PictureActions actions;
};

struct EmotionPrefix {
    ObjectFlags object;
    std::byte before_rectangle[0x1cc - sizeof(ObjectFlags)];
    Rectangle rectangle;
};

struct AimingTargetPrefix {
    ObjectFlags object;
    std::byte before_bounds[0x1c - sizeof(ObjectFlags)];
    std::uint16_t bounds[4];
};

struct MovieInputPrefix {
    std::byte before_skippable[0x2c];
    unsigned skippable;
};

struct PreferencesPrefix {
    std::byte before_skip_movies[0x74];
    unsigned skip_movies;
};

struct RuntimeActionPrefix {
    std::byte before_resource[0x13c];
    void* resource;
};

struct InventoryActionResourcePrefix {
    ResourceHeader resource;
    std::byte before_action[0x38 - sizeof(ResourceHeader)];
    unsigned action;
};

struct PictureResourcePrefix {
    ResourceHeader resource;
    std::byte before_rectangle[0x30 - sizeof(ResourceHeader)];
    Rectangle rectangle;
    unsigned cursor, conversation, evidence;
};

struct NavigationResourcePrefix {
    ResourceHeader resource;
    std::byte before_shape[0x2c - sizeof(ResourceHeader)];
    unsigned shape;
};

namespace state_offset {
inline constexpr auto navigation = offsetof(SceneStatePrefix, navigation);
inline constexpr auto pictures = offsetof(SceneStatePrefix, pictures);
inline constexpr auto emotions = offsetof(SceneStatePrefix, emotions);
inline constexpr auto owned_inventory = offsetof(SceneStatePrefix, owned_inventory);
inline constexpr auto hotspots = offsetof(SceneStatePrefix, hotspots);
inline constexpr auto selection_first = offsetof(SceneStatePrefix, selection_first);
inline constexpr auto selection_second = offsetof(SceneStatePrefix, selection_second);
inline constexpr auto modal = offsetof(SceneStatePrefix, modal);
inline constexpr auto aiming_targets = offsetof(SceneStatePrefix, aiming_targets);
}

namespace main_view_offset {
inline constexpr auto modal_background = offsetof(ModalViewPrefix, background);
inline constexpr auto modal_foreground = offsetof(ModalViewPrefix, foreground);
}

namespace object_offset {
inline constexpr auto runtime_action_resource = offsetof(RuntimeActionPrefix, resource);
inline constexpr auto inventory_graphic = offsetof(OwnedInventoryItemPrefix, graphic);
inline constexpr auto picture_actions = offsetof(WorldPicturePrefix, actions);
inline constexpr auto aiming_bounds = offsetof(AimingTargetPrefix, bounds);
inline constexpr auto emotion_rectangle = offsetof(EmotionPrefix, rectangle);
inline constexpr auto movie_skippable = offsetof(MovieInputPrefix, skippable);
}

namespace resource_offset {
inline constexpr auto inventory_action = offsetof(InventoryActionResourcePrefix, action);
inline constexpr auto picture_rectangle = offsetof(PictureResourcePrefix, rectangle);
inline constexpr auto picture_cursor = offsetof(PictureResourcePrefix, cursor);
inline constexpr auto picture_conversation = offsetof(PictureResourcePrefix, conversation);
inline constexpr auto picture_evidence = offsetof(PictureResourcePrefix, evidence);
inline constexpr auto navigation_shape = offsetof(NavigationResourcePrefix, shape);
}

namespace object_vtable_slot {
inline constexpr std::size_t resource = 1;
}

namespace preferences_offset {
inline constexpr auto skip_movies = offsetof(PreferencesPrefix, skip_movies);
}

static_assert(sizeof(ObjectFlags) == 0x10);
static_assert(offsetof(ObjectFlags, enabled) == 8);
static_assert(offsetof(ObjectFlags, blocked) == 12);
static_assert(sizeof(ResourceObject) == 0x1c);
static_assert(offsetof(ResourceObject, resource) == 0x18);
static_assert(sizeof(ResourceHeader) == 8);
static_assert(offsetof(ResourceHeader, id) == 4);
static_assert(sizeof(PictureActions) == 0x1c);
static_assert(sizeof(RuntimeActionExpression) == 12);
static_assert(offsetof(RuntimeActionExpression, left_kind) == 8);
static_assert(offsetof(RuntimeActionExpression, operation) == 10);
static_assert(state_offset::navigation == 0x68);
static_assert(state_offset::pictures == 0x7c);
static_assert(state_offset::emotions == 0x130);
static_assert(state_offset::owned_inventory == 0x144);
static_assert(state_offset::hotspots == 0x194);
static_assert(state_offset::selection_first == 0x1a8);
static_assert(state_offset::selection_second == 0x1e4);
static_assert(state_offset::modal == 0x20c);
static_assert(state_offset::aiming_targets == 0x248);
static_assert(main_view_offset::modal_background == 0x59c);
static_assert(main_view_offset::modal_foreground == 0x5d8);
static_assert(object_offset::runtime_action_resource == 0x13c);
static_assert(object_offset::inventory_graphic == 0x1e4);
static_assert(object_offset::picture_actions == 0x148);
static_assert(object_offset::aiming_bounds == 0x1c);
static_assert(object_offset::emotion_rectangle == 0x1cc);
static_assert(object_offset::movie_skippable == 0x2c);
static_assert(resource_offset::inventory_action == 0x38);
static_assert(resource_offset::picture_rectangle == 0x30);
static_assert(resource_offset::picture_cursor == 0x44);
static_assert(resource_offset::picture_conversation == 0x48);
static_assert(resource_offset::picture_evidence == 0x4c);
static_assert(resource_offset::navigation_shape == 0x2c);
static_assert(preferences_offset::skip_movies == 0x74);
}
