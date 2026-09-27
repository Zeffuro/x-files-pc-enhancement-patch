#pragma once
#include <cstddef>
#include <cstdint>

namespace native_game {
struct Variable {
    std::uint32_t vtable;
    std::uint32_t id;
    std::byte reserved[0x30];
    std::int32_t raw_value;
    std::byte before_type[5];
    std::uint8_t type_flags;
};

static_assert(offsetof(Variable, id) == 4);
static_assert(offsetof(Variable, raw_value) == 0x38);
static_assert(offsetof(Variable, type_flags) == 0x41);
}
