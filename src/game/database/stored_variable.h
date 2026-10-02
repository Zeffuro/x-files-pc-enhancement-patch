#pragma once
#include "offline_index.h"
#include <optional>

namespace game_assets {
struct StoredVariable {
    std::uint32_t offset = 0, name_offset = 0, name_size = 0;
    std::string name;
    std::uint32_t value_bits = 0, owner_id = 0;
    std::int32_t signed_value = 0;
    std::uint8_t flag = 0, type = 0;
};

std::optional<StoredVariable> parse_stored_variable(std::span<const std::uint8_t> bytes,
                                                    const OfflineDatabaseIndex& index,
                                                    std::uint32_t class_id, std::uint32_t id);
}
