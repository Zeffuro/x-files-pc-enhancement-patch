#pragma once
#include <cstddef>
#include <cstdint>
#include "story.h"

namespace native_game {
enum class VariableKind : std::uint8_t { signed_byte = 0, integer = 1, boolean = 2 };
inline constexpr std::uint8_t variable_type_mask = 0x7f;

struct Variable {
    std::uint32_t vtable;
    std::uint32_t id;
    std::byte reserved[0x20];
    StoryString name;
    std::int32_t raw_value;
    std::byte before_type[5];
    std::uint8_t type_flags;
};

static_assert(offsetof(Variable, id) == 4);
static_assert(offsetof(Variable, name) == 0x28);
static_assert(sizeof(Variable) == 0x44);
static_assert(offsetof(Variable, raw_value) == 0x38);
static_assert(offsetof(Variable, type_flags) == 0x41);
}
