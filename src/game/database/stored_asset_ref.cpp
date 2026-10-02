#include "stored_asset_ref.h"
#include <algorithm>

namespace game_assets {
std::optional<StoredAssetReference>
parse_stored_asset_reference(std::span<const std::uint8_t> bytes, const OfflineDatabaseIndex& index,
                             std::uint32_t class_id, std::uint32_t id) {
    if (class_id != 0x35 || !id) {
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
    const auto conflict = [&](std::size_t at, std::size_t extent, bool descriptor = false) {
        if (at < 32 || !has(at, extent)) {
            return true;
        }
        const auto end = at + extent;
        const auto range =
            std::lower_bound(index.node_ranges.begin(), index.node_ranges.end(), at,
                             [](const auto& range, auto start) { return range.second <= start; });
        if (range != index.node_ranges.end() && range->first < end) {
            return true;
        }
        const auto definition =
            std::lower_bound(index.definition_offsets.begin(), index.definition_offsets.end(),
                             at + (descriptor ? 1 : 0));
        return definition != index.definition_offsets.end() && *definition < end;
    };
    const auto offset = std::size_t(index.records[*found].offset);
    const auto aliases =
        std::equal_range(index.definition_offsets.begin(), index.definition_offsets.end(), offset);
    if (conflict(offset, 24, true) || std::distance(aliases.first, aliases.second) != 1 ||
        !(bytes[offset] & 0x80) || word(offset + 2) != 1) {
        return {};
    }
    StoredAssetReference result;
    result.offset = static_cast<std::uint32_t>(offset);
    result.flags = {bytes[offset], bytes[offset + 1]};
    result.name_mark = word(offset + 6);
    result.name_size = word(offset + 10);
    result.raw_words = {word(offset + 14), word(offset + 18)};
    result.raw_bytes = {bytes[offset + 22], bytes[offset + 23]};
    if (result.name_size > 65536 || (!result.name_mark && result.name_size)) {
        return {};
    }
    if (result.name_size) {
        const auto mark = std::size_t(result.name_mark);
        if (conflict(mark, result.name_size) ||
            (mark < offset + 24 && offset < mark + result.name_size)) {
            return {};
        }
        result.name.assign(bytes.begin() + mark, bytes.begin() + mark + result.name_size);
    }
    return result;
}
}
