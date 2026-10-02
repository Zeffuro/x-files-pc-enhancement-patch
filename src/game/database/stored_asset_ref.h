#pragma once
#include "offline_index.h"
#include <array>

namespace game_assets {
struct StoredAssetReference {
    std::uint32_t offset = 0, name_mark = 0, name_size = 0;
    std::array<std::uint8_t, 2> flags{}, raw_bytes{};
    std::array<std::uint32_t, 2> raw_words{};
    std::vector<std::uint8_t> name;
};

std::optional<StoredAssetReference>
parse_stored_asset_reference(std::span<const std::uint8_t> bytes, const OfflineDatabaseIndex& index,
                             std::uint32_t class_id, std::uint32_t id);
}
