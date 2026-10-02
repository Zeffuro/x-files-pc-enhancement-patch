#pragma once
#include "offline_index.h"
#include "stored_asset_list.h"
#include <array>

namespace game_assets {
bool supports_stored_fields(std::uint32_t class_id);

struct StoredScalarField {
    std::uint32_t offset = 0, value = 0;
    unsigned width = 0;
};

struct StoredBlobField {
    std::uint32_t offset = 0, mark = 0;
    std::vector<std::uint8_t> bytes;
    bool word_array = false;
    std::vector<std::uint32_t> little_endian_words;
};

struct StoredFields {
    std::uint32_t offset = 0, extent = 0;
    std::array<std::uint8_t, 2> flags{};
    std::vector<StoredScalarField> scalars;
    std::vector<StoredBlobField> blobs;
    std::optional<StoredAssetList> list;
};

std::optional<StoredFields> parse_stored_fields(std::span<const std::uint8_t> bytes,
                                                const OfflineDatabaseIndex& index,
                                                std::uint32_t class_id, std::uint32_t id);
}
