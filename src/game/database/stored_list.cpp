#include "stored_list.h"
#include <algorithm>
#include <utility>

namespace game_assets {
std::optional<StoredReferenceList> parse_stored_reference_list(std::span<const std::uint8_t> bytes,
                                                               const OfflineDatabaseIndex& index,
                                                               std::uint32_t class_id,
                                                               std::uint32_t id) {
    if (class_id != 0x42 && class_id != 0x52) {
        return {};
    }
    const auto found = find_database_record(index, class_id, id);
    if (!found) {
        return {};
    }
    const auto has = [&](std::size_t at, std::size_t count) {
        return at <= bytes.size() && count <= bytes.size() - at;
    };
    const auto word = [&](std::size_t at) {
        return (std::uint32_t(bytes[at]) << 24) | (std::uint32_t(bytes[at + 1]) << 16) |
               (std::uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
    };
    if (bytes.size() < 32 || bytes.size() > 128u * 1024u * 1024u || word(0) != 5 ||
        word(20) != 0x501 || word(24) != 0x40000 || word(28) != 256) {
        return {};
    }
    const auto offset = std::size_t(index.records[*found].offset);
    const auto overlaps_index = [&](std::size_t start, std::size_t end) {
        const auto range =
            std::lower_bound(index.node_ranges.begin(), index.node_ranges.end(), start,
                             [](const auto& range, auto start) { return range.second <= start; });
        return range != index.node_ranges.end() && range->first < end;
    };
    if (offset < 32 || !has(offset, 32) || !(bytes[offset] & 0x80) || word(offset + 2) != 1 ||
        word(offset + 6) > 4096 || word(offset + 14) != 0x0b || word(offset + 22) != 1 ||
        word(offset + 28) != 0 || overlaps_index(offset, offset + 32)) {
        return {};
    }
    StoredReferenceList result;
    result.offset = static_cast<std::uint32_t>(offset);
    result.resource_mark = word(offset + 10);
    result.resource_class = word(offset + 14);
    result.resource_type = word(offset + 18);
    result.resource_owner = word(offset + 22);
    result.needs_release_raw = bytes[offset + 26];
    result.duplicate_raw = bytes[offset + 27];
    result.resource_id = word(offset + 28);
    result.target_class = class_id == 0x42 ? 0x41u : 0x51u;
    const auto expected = std::size_t(word(offset + 6));
    if (!result.resource_mark) {
        return expected == 0 ? std::optional{result} : std::nullopt;
    }
    std::vector<std::pair<std::size_t, std::size_t>> ranges{{offset, offset + 32}};
    result.ids.reserve(expected);
    const auto read = [&](auto&& self, std::size_t mark, unsigned depth) -> bool {
        if (depth >= 32 || result.resource_nodes >= 512 || mark < 32 || !has(mark, 8) ||
            word(mark + 2) != 1 || bytes[mark + 6] != 0) {
            return false;
        }
        const auto count = std::size_t(bytes[mark + 7]);
        const auto extent = 8 + count * 4;
        if (!has(mark, extent) || overlaps_index(mark, mark + extent)) {
            return false;
        }
        for (const auto& range : ranges) {
            if (mark < range.second && range.first < mark + extent) {
                return false;
            }
        }
        ranges.emplace_back(mark, mark + extent);
        ++result.resource_nodes;
        result.decoded_resource_bytes += extent;
        if (bytes[mark] & 0x80) {
            if (count > expected - result.ids.size()) {
                return false;
            }
            for (std::size_t i = 0; i < count; ++i) {
                result.ids.push_back(word(mark + 8 + i * 4));
            }
        } else {
            for (std::size_t i = 0; i < count; ++i) {
                if (!self(self, word(mark + 8 + i * 4), depth + 1)) {
                    return false;
                }
            }
        }
        return true;
    };
    if (!read(read, result.resource_mark, 0) || result.ids.size() != expected) {
        return {};
    }
    return result;
}
}
