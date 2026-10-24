#pragma once
#include "database/cache.h"

namespace native_game {

struct AiActionListCursor;

// The native cursor allocates 0x34 bytes. Only this prefix is verified.
struct AiActionListCursorPrefix {
    void* vtable;
    std::byte before_element_index[0x0a - sizeof(void*)];
    std::uint16_t element_index;
    std::byte before_element_source[0x10];
    void* element_source;
};

struct AiActionListStoragePrefix {
    void* vtable;
    std::byte before_cursor[0x38 - sizeof(void*)];
    AiActionListCursor* cursor;
};

struct AiActionListResourcePrefix {
    PersistentObject object;
    AiActionListStoragePrefix actions;
};

struct AiActionListCursorGuard {
    void* vtable;
    AiActionListStoragePrefix* actions;
    // Only a guard that created the cursor destroys it when detached.
    std::uint32_t owns_cursor;
};

struct AiStatePrefix {
    AiActionListCursorGuard* list_cursor;
    std::uint32_t glow_view, glowing;
};

namespace ai_offset {
inline constexpr auto cursor_element_index = offsetof(AiActionListCursorPrefix, element_index);
inline constexpr auto cursor_element_source = offsetof(AiActionListCursorPrefix, element_source);
inline constexpr auto resource_actions = offsetof(AiActionListResourcePrefix, actions);
inline constexpr auto storage_cursor = offsetof(AiActionListStoragePrefix, cursor);
inline constexpr auto guard_actions = offsetof(AiActionListCursorGuard, actions);
inline constexpr auto guard_owns_cursor = offsetof(AiActionListCursorGuard, owns_cursor);
inline constexpr auto glow_view = offsetof(AiStatePrefix, glow_view);
inline constexpr auto glowing = offsetof(AiStatePrefix, glowing);
}

static_assert(sizeof(void*) == 4);
static_assert(ai_offset::cursor_element_index == 0x0a);
static_assert(ai_offset::cursor_element_source == 0x1c);
static_assert(ai_offset::resource_actions == 0x28);
static_assert(ai_offset::storage_cursor == 0x38);
static_assert(ai_offset::guard_actions == 4);
static_assert(ai_offset::guard_owns_cursor == 8);
static_assert(ai_offset::glow_view == 4);
static_assert(ai_offset::glowing == 8);
static_assert(sizeof(AiActionListStoragePrefix) == 0x3c);
static_assert(sizeof(AiActionListResourcePrefix) == 0x64);
static_assert(sizeof(AiActionListCursorGuard) == 12);
static_assert(sizeof(AiStatePrefix) == 12);
static_assert(sizeof(AiActionListCursorPrefix) == 0x20);

}
