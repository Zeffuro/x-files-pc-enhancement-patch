#pragma once
#include "offline_index.h"

namespace game_assets {
struct StoredReferenceList {
    std::uint32_t offset = 0, resource_mark = 0, resource_class = 0, resource_type = 0;
    std::uint32_t resource_owner = 0, resource_id = 0, target_class = 0;
    std::uint8_t needs_release_raw = 0, duplicate_raw = 0;
    std::size_t resource_nodes = 0, decoded_resource_bytes = 0;
    std::vector<std::uint32_t> ids;
};

std::optional<StoredReferenceList> parse_stored_reference_list(std::span<const std::uint8_t> bytes,
                                                               const OfflineDatabaseIndex& index,
                                                               std::uint32_t class_id,
                                                               std::uint32_t id);
}
