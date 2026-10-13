#pragma once
#include "ui.h"
#include "native_objects.h"

namespace native_game {
struct ScriptInputPrefix {
    void* vtable;
    unsigned visible, enabled, disabled;
    std::byte reserved[8];
    void* resource;
    List<void> actions;
};

struct ScriptControlPrefix {
    ScriptInputPrefix input;
    std::byte reserved[0x140 - sizeof(ScriptInputPrefix)];
    void* graphic;
    unsigned id;
};

struct ScriptTextOwner {
    void* vtable;
    void* storage_type;
    unsigned count, current;
    void** entries;
};

struct TextRowPrefix {
    void* text;
    NativePoint point;
    void* unknown;
};

struct MainTextGroupPrefix {
    void* vtable;
    List<std::byte> rows;
};

struct TextPrefix {
    std::byte reserved[0x28];
    const char* string;
};

struct InputGraphicTextPrefix {
    std::byte reserved[0x154];
    void* text;
};

struct PhotoThumbnailPrefix {
    void* vtable;
    std::byte reserved[0x14];
    std::byte graphic[0x44];
    void* photo;
    std::byte before_point[8];
    NativePoint point;
};

struct PhotoGallery {
    List<std::byte> photos;
    unsigned first;
};

struct MainViewScriptPrefix {
    std::byte before_text[0x4c8];
    ScriptTextOwner text;
    std::byte before_photos[0x52c - 0x4c8 - sizeof(ScriptTextOwner)];
    Inventory photos;
};

struct ChoiceViewportPrefix {
    Rectangle viewport;
    std::byte before_scroll[0x9c - sizeof(Rectangle)];
    unsigned scroll_position;
};

namespace view_offset {
inline constexpr auto text_owner = offsetof(MainViewScriptPrefix, text);
inline constexpr auto photo_owner = offsetof(MainViewScriptPrefix, photos);
inline constexpr auto choice_scroll_from_viewport = offsetof(ChoiceViewportPrefix, scroll_position);
}

static_assert(view_offset::text_owner == 0x4c8);
static_assert(view_offset::photo_owner == 0x52c);
static_assert(view_offset::choice_scroll_from_viewport == 0x9c);

struct ScriptStatePrefix {
    SceneStatePrefix scene;
    List<void> roots, controls;
    std::byte before_photos[0x298 - 0x284];
    List<void> photos;
};

namespace state_offset {
inline constexpr auto script_roots = offsetof(ScriptStatePrefix, roots);
inline constexpr auto script_controls = offsetof(ScriptStatePrefix, controls);
inline constexpr auto photos = offsetof(ScriptStatePrefix, photos);
}

static_assert(sizeof(SceneStatePrefix) == 0x25c);
static_assert(state_offset::script_roots == 0x25c);
static_assert(state_offset::script_controls == 0x270);
static_assert(state_offset::photos == 0x298);

static_assert(offsetof(ScriptInputPrefix, enabled) == 8);
static_assert(offsetof(ScriptInputPrefix, disabled) == 12);
static_assert(offsetof(ScriptInputPrefix, resource) == 0x18);
static_assert(offsetof(ScriptInputPrefix, actions) == 0x1c);
static_assert(offsetof(ScriptControlPrefix, graphic) == 0x140);
static_assert(offsetof(ScriptControlPrefix, id) == 0x144);
static_assert(offsetof(ScriptTextOwner, count) == 8);
static_assert(offsetof(ScriptTextOwner, current) == 12);
static_assert(offsetof(ScriptTextOwner, entries) == 16);
static_assert(offsetof(MainTextGroupPrefix, rows) == 4);
static_assert(offsetof(TextPrefix, string) == 0x28);
static_assert(offsetof(InputGraphicTextPrefix, text) == 0x154);
static_assert(sizeof(TextRowPrefix) == 20);
static_assert(offsetof(PhotoThumbnailPrefix, graphic) == 0x18);
static_assert(offsetof(PhotoThumbnailPrefix, photo) == 0x5c);
static_assert(offsetof(PhotoThumbnailPrefix, point) == 0x68);
static_assert(offsetof(PhotoGallery, first) == 0x14);
}
