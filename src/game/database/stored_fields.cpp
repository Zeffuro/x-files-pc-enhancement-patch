#include "stored_fields.h"
#include <algorithm>
#include <numeric>

namespace game_assets {
namespace {
std::span<const unsigned> schema(std::uint32_t class_id) {
    static constexpr unsigned one[] = {4}, two[] = {4, 4}, three[] = {4, 4, 4};
    static constexpr unsigned six[] = {4, 4, 4, 4, 4, 4}, seven[] = {4, 4, 4, 4, 4, 4, 4};
    static constexpr unsigned name[] = {8, 8}, string[] = {8};
    static constexpr unsigned enabled[] = {4, 1, 1}, category[] = {4, 1};
    static constexpr unsigned point[] = {2, 2, 4, 4}, rectangle[] = {4, 2, 2, 2, 2, 4};
    static constexpr unsigned interface_item[] = {4, 2, 2, 2, 2, 4, 4, 2, 2};
    static constexpr unsigned title[] = {4, 4, 4, 4, 4, 1, 1, 4, 8, 4, 8, 8, 4, 4, 2, 2};
    static constexpr unsigned node[] = {4, 4, 4, 4, 4, 1, 1, 4, 4, 4};
    static constexpr unsigned location[] = {4, 4, 4, 4, 4, 1, 1, 4, 4, 4, 4, 1};
    static constexpr unsigned viewpoint[] = {4, 4, 4, 4, 4, 1, 1, 4, 2, 2, 4, 4};
    static constexpr unsigned character_view[] = {4, 4, 2, 2, 2, 2, 4, 4, 4, 4};
    static constexpr unsigned conversation[] = {4, 4, 4, 4, 4, 4, 1};
    static constexpr unsigned photo[] = {8, 4, 4, 2, 2, 2, 2, 1, 1};
    static constexpr unsigned state[] = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8,
                                         8, 8, 8, 1, 1, 1, 1, 1, 1, 1, 4, 1, 4, 4, 4, 2};
    switch (class_id) {
        case 0x27:
            return title;
        case 0x28:
            return node;
        case 0x29:
            return location;
        case 0x2a:
            return viewpoint;
        case 0x2b:
            return six;
        case 0x2c:
            return one;
        case 0x2d:
            return character_view;
        case 0x31:
            return conversation;
        case 0x2f:
            return name;
        case 0x33:
        case 0x3e:
        case 0x40:
        case 0x54:
            return three;
        case 0x37:
        case 0x39:
            return rectangle;
        case 0x3c:
            return category;
        case 0x3d:
            return point;
        case 0x46:
            return enabled;
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
            return seven;
        case 0x4c:
            return two;
        case 0x4f:
            return interface_item;
        case 0x50:
        case 0x56:
        case 0x58:
        case 0x5a:
        case 0x5b:
        case 0x5c:
            return string;
        case 0x57:
            return state;
        case 0x59:
            return photo;
        default:
            return {};
    }
}
}

bool supports_stored_fields(std::uint32_t class_id) {
    return !schema(class_id).empty();
}

std::optional<StoredFields> parse_stored_fields(std::span<const std::uint8_t> bytes,
                                                const OfflineDatabaseIndex& index,
                                                std::uint32_t class_id, std::uint32_t id) {
    const auto widths = schema(class_id);
    if (widths.empty()) {
        return {};
    }
    const auto extent = std::accumulate(widths.begin(), widths.end(), std::size_t{6});
    const auto found = find_database_record(index, class_id, id);
    if (!found || !id) {
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
    const auto conflict = [&](std::size_t at, std::size_t size, bool descriptor = false) {
        if (at < 32 || !has(at, size)) {
            return true;
        }
        const auto range =
            std::lower_bound(index.node_ranges.begin(), index.node_ranges.end(), at,
                             [](const auto& range, auto start) { return range.second <= start; });
        const auto definition =
            std::lower_bound(index.definition_offsets.begin(), index.definition_offsets.end(),
                             at + (descriptor ? 1 : 0));
        return (range != index.node_ranges.end() && range->first < at + size) ||
               (definition != index.definition_offsets.end() && *definition < at + size);
    };
    const auto offset = std::size_t(index.records[*found].offset);
    const auto aliases =
        std::equal_range(index.definition_offsets.begin(), index.definition_offsets.end(), offset);
    if (conflict(offset, extent, true) || std::distance(aliases.first, aliases.second) != 1 ||
        !(bytes[offset] & 0x80) || word(offset + 2) != 1) {
        return {};
    }
    StoredFields result;
    result.offset = static_cast<std::uint32_t>(offset);
    result.extent = static_cast<std::uint32_t>(extent);
    result.flags = {bytes[offset], bytes[offset + 1]};
    unsigned at = 6;
    for (const auto width : widths) {
        if (width == 8) {
            const auto mark = std::size_t(word(offset + at));
            const auto size = std::size_t(word(offset + at + 4));
            if (size > 65536 || (!mark && size) ||
                (size &&
                 (conflict(mark, size) || (mark < offset + extent && offset < mark + size)))) {
                return {};
            }
            StoredBlobField blob;
            blob.offset = at;
            blob.mark = static_cast<std::uint32_t>(mark);
            if (size) {
                blob.bytes.assign(bytes.begin() + mark, bytes.begin() + mark + size);
            }
            blob.word_array =
                class_id == 0x56 || class_id == 0x58 || (class_id >= 0x5a && class_id <= 0x5c);
            if (blob.word_array) {
                for (std::size_t i = 0; i + 4 <= blob.bytes.size(); i += 4) {
                    blob.little_endian_words.push_back(std::uint32_t(blob.bytes[i]) |
                                                       (std::uint32_t(blob.bytes[i + 1]) << 8) |
                                                       (std::uint32_t(blob.bytes[i + 2]) << 16) |
                                                       (std::uint32_t(blob.bytes[i + 3]) << 24));
                }
            }
            result.blobs.push_back(std::move(blob));
        } else {
            std::uint32_t value = 0;
            for (unsigned byte = 0; byte < width; ++byte) {
                value = (value << 8) | bytes[offset + at + byte];
            }
            result.scalars.push_back({at, value, width});
        }
        at += width;
    }
    if (class_id >= 0x27 && class_id <= 0x2a) {
        result.list = parse_stored_object_list(bytes, index, class_id, id);
        if (!result.list) {
            return {};
        }
        for (const auto& blob : result.blobs) {
            for (const auto& node : result.list->nodes) {
                if (!blob.bytes.empty() && blob.mark < node.offset + node.extent &&
                    node.offset < std::size_t(blob.mark) + blob.bytes.size()) {
                    return {};
                }
            }
        }
    }
    return result;
}
}
