#pragma once
#include "cache.h"
#include <array>

namespace native_game {
struct StoryHotspot {
    PersistentObject object;
    std::uint32_t shape_vtable, rectangle_vtable;
    std::int32_t left, top, right, bottom;
    std::uint32_t shape_value, name_id;
};

struct StoryStandardAction {
    PersistentObject object;
    std::uint32_t name_id, raw_value, action_id;
};

struct StoryTitle {
    PersistentObject object;
    std::byte reserved[0x74];
    std::uint32_t name_id, trigger_list_id;
    std::uint16_t raw_a4, raw_a6;
};

struct StoryString {
    std::uint32_t vtable, data, capacity, unknown;
};

struct StoryName {
    PersistentObject object;
    std::array<StoryString, 2> strings;
};

struct StoryReferenceList {
    PersistentObject object;
    std::byte reserved[0x30];
    std::uint32_t cached_resource, raw_5c, iterator;
};

template <class T, std::size_t N> struct StoryParameterArray {
    std::uint32_t vtable, length;
    std::int32_t iterator;
    std::uint32_t data, capacity;
    std::array<T, N> inline_values;
};

struct StoryContext {
    std::uint32_t vtable;
    StoryParameterArray<std::uint32_t, 5> values;
};

struct StoryTrigger {
    PersistentObject object;
    StoryParameterArray<std::uint32_t, 8> integers;
    StoryParameterArray<std::uint32_t, 12> booleans;
    StoryParameterArray<std::int8_t, 4> bytes;
    StoryContext context;
    std::uint32_t action_list_id;
    std::uint8_t raw_type;
    std::byte alignment[3];
};

struct StoryAction {
    PersistentObject object;
    std::byte reserved[0x34];
    std::uint32_t payload_size, cached_payload;
    std::uint8_t raw_type;
    std::byte alignment[3];
    std::uint32_t runtime_reference;
};

static_assert(sizeof(StoryHotspot) == 0x48);
static_assert(offsetof(StoryHotspot, left) == 0x30);
static_assert(offsetof(StoryHotspot, name_id) == 0x44);
static_assert(sizeof(StoryStandardAction) == 0x34);
static_assert(offsetof(StoryStandardAction, action_id) == 0x30);
static_assert(sizeof(StoryTitle) == 0xa8);
static_assert(offsetof(StoryTitle, name_id) == 0x9c);
static_assert(offsetof(StoryTitle, trigger_list_id) == 0xa0);
static_assert(sizeof(StoryName) == 0x48);
static_assert(offsetof(StoryName, strings) == 0x28);
static_assert(sizeof(StoryReferenceList) == 0x64);
static_assert(offsetof(StoryReferenceList, cached_resource) == 0x58);
static_assert(sizeof(StoryContext) == 0x2c);
static_assert(offsetof(StoryContext, values) == 4);
static_assert(sizeof(StoryTrigger) == 0xec);
static_assert(offsetof(StoryTrigger, integers) == 0x28);
static_assert(offsetof(StoryTrigger, booleans) == 0x5c);
static_assert(offsetof(StoryTrigger, bytes) == 0xa0);
static_assert(offsetof(StoryTrigger, context) == 0xb8);
static_assert(offsetof(StoryTrigger, action_list_id) == 0xe4);
static_assert(offsetof(StoryTrigger, raw_type) == 0xe8);
static_assert(sizeof(StoryAction) == 0x6c);
static_assert(offsetof(StoryAction, payload_size) == 0x5c);
static_assert(offsetof(StoryAction, cached_payload) == 0x60);
static_assert(offsetof(StoryAction, raw_type) == 0x64);
}
