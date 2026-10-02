#pragma once
#include "offline_index.h"
#include <array>

namespace game_assets {
struct StoredAssetListNode {
    std::uint32_t offset = 0;
    std::size_t extent = 0;
    std::array<std::uint8_t, 2> flags{};
};

struct StoredAssetList {
    std::uint32_t offset = 0, resource_mark = 0, resource_class = 0, resource_type = 0;
    std::uint32_t resource_owner = 0, resource_id = 0;
    std::uint32_t descriptor_extent = 32, trailing_word = 0;
    std::array<std::uint8_t, 2> flags{};
    std::uint8_t needs_release_raw = 0, duplicate_raw = 0;
    std::uint8_t trailing_byte = 0;
    std::size_t decoded_resource_bytes = 0;
    std::vector<std::uint32_t> ids;
    std::vector<StoredAssetListNode> nodes;
};

std::optional<StoredAssetList> parse_stored_asset_list(std::span<const std::uint8_t> bytes,
                                                       const OfflineDatabaseIndex& index,
                                                       std::uint32_t class_id, std::uint32_t id);
std::optional<StoredAssetList> parse_stored_object_list(std::span<const std::uint8_t> bytes,
                                                        const OfflineDatabaseIndex& index,
                                                        std::uint32_t class_id, std::uint32_t id);
}
