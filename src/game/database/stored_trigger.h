#pragma once
#include "offline_index.h"

namespace game_assets {
struct StoredTrigger {
    std::uint32_t offset = 0, action_list_id = 0;
    std::uint8_t event_type = 0;
};

std::optional<StoredTrigger> parse_stored_trigger(std::span<const std::uint8_t> bytes,
                                                  const OfflineDatabaseIndex& index,
                                                  std::uint32_t class_id, std::uint32_t id);
}
