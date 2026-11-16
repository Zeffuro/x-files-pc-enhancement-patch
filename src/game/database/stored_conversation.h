#pragma once
#include "offline_index.h"

namespace game_assets {
struct StoredConversation {
    std::uint32_t id = 0, offset = 0, name_id = 0, icons_id = 0, responses_id = 0;
    std::uint32_t triggers_id = 0, question_id = 0, atoms_id = 0;
    std::uint8_t raw_flag = 0;
    std::vector<std::uint8_t> name;
    bool name_available = false;
};

struct StoredConversationList {
    std::uint32_t id = 0;
    std::vector<std::uint32_t> conversations;
};

std::optional<StoredConversation> parse_stored_conversation(std::span<const std::uint8_t> bytes,
                                                            const OfflineDatabaseIndex& index,
                                                            std::uint32_t class_id,
                                                            std::uint32_t id);
std::optional<StoredConversationList>
parse_stored_conversations(std::span<const std::uint8_t> bytes, const OfflineDatabaseIndex& index,
                           std::uint32_t class_id, std::uint32_t id);
}
