#include "stored_trigger.h"

namespace game_assets {
std::optional<StoredTrigger> parse_stored_trigger(std::span<const std::uint8_t> bytes,
                                                  const OfflineDatabaseIndex& index,
                                                  std::uint32_t class_id, std::uint32_t id) {
    if (class_id != 0x51) {
        return {};
    }
    const auto found = find_database_record(index, class_id, id);
    if (!found) {
        return {};
    }
    const auto word = [&](std::size_t at) {
        return (std::uint32_t(bytes[at]) << 24) | (std::uint32_t(bytes[at + 1]) << 16) |
               (std::uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
    };
    if (bytes.size() < 32 || bytes.size() > 128u * 1024u * 1024u || word(0) != 5 ||
        word(20) != 0x501 || word(24) != 0x40000 || word(28) != 256) {
        return {};
    }
    const auto offset = std::size_t(index.records[*found].offset);
    if (offset < 32 || offset > bytes.size() || 11 > bytes.size() - offset ||
        word(offset + 2) != 1) {
        return {};
    }
    return StoredTrigger{static_cast<std::uint32_t>(offset), word(offset + 6), bytes[offset + 10]};
}
}
