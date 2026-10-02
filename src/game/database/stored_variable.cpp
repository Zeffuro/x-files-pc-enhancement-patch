#include "stored_variable.h"
#include <algorithm>
#include <bit>

namespace game_assets {
std::optional<StoredVariable> parse_stored_variable(std::span<const std::uint8_t> bytes,
                                                    const OfflineDatabaseIndex& index,
                                                    std::uint32_t class_id, std::uint32_t id) {
    if (class_id != 0x53) {
        return {};
    }
    const auto found = find_database_record(index, class_id, id);
    if (!found) {
        return {};
    }
    const auto offset = std::size_t(index.records[*found].offset);
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
    if (!has(offset, 24) || word(offset + 2) != 1) {
        return {};
    }
    StoredVariable result;
    result.offset = static_cast<std::uint32_t>(offset);
    result.name_offset = word(offset + 6);
    result.name_size = word(offset + 10);
    if (result.name_offset < 32 || result.name_size < 2 || result.name_size > 1024 ||
        !has(result.name_offset, result.name_size)) {
        return {};
    }
    const auto name = bytes.subspan(result.name_offset, result.name_size);
    if (name.back() != 0 || !std::all_of(name.begin(), name.end() - 1,
                                         [](auto ch) { return ch >= 0x20 && ch <= 0x7e; })) {
        return {};
    }
    result.name.assign(reinterpret_cast<const char*>(name.data()), name.size() - 1);
    result.value_bits = word(offset + 14);
    result.signed_value = std::bit_cast<std::int32_t>(result.value_bits);
    result.owner_id = word(offset + 18);
    result.flag = bytes[offset + 22];
    result.type = bytes[offset + 23];
    return result;
}
}
