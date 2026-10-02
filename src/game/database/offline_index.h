#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace game_assets {
struct StoredDatabaseRecord {
    std::uint32_t class_id = 0, id = 0, offset = 0;
    bool operator==(const StoredDatabaseRecord&) const = default;
};

struct OfflineDatabaseLimits {
    std::size_t max_nodes = 8192, max_records = 200000;
    unsigned max_depth = 32;
};

struct OfflineDatabaseIndex {
    std::vector<StoredDatabaseRecord> records;
    std::vector<std::uint32_t> definition_offsets;
    std::vector<std::pair<std::size_t, std::size_t>> node_ranges;
    std::size_t visited_nodes = 0, skipped_nodes = 0, unsupported_indexes = 0;
    std::size_t duplicate_keys = 0;
    bool truncated = false, available = false;
    std::wstring status;
};

OfflineDatabaseIndex parse_database_index(std::span<const std::uint8_t> bytes,
                                          OfflineDatabaseLimits limits = {});
std::optional<std::size_t> find_database_record(const OfflineDatabaseIndex& index,
                                                std::uint32_t class_id, std::uint32_t id);
}
