#include "stored_conversation.h"
#include "stored_fields.h"
#include <algorithm>

namespace game_assets {
namespace {
std::optional<std::uint32_t> scalar(const StoredFields& fields, std::uint32_t offset,
                                    unsigned width) {
    const auto found =
        std::find_if(fields.scalars.begin(), fields.scalars.end(), [&](const auto& field) {
            return field.offset == offset && field.width == width;
        });
    return found == fields.scalars.end() ? std::nullopt : std::optional{found->value};
}
}

std::optional<StoredConversation> parse_stored_conversation(std::span<const std::uint8_t> bytes,
                                                            const OfflineDatabaseIndex& index,
                                                            std::uint32_t class_id,
                                                            std::uint32_t id) {
    if (class_id != 0x31) {
        return {};
    }
    const auto fields = parse_stored_fields(bytes, index, class_id, id);
    if (!fields) {
        return {};
    }
    const auto name = scalar(*fields, 6, 4), icons = scalar(*fields, 10, 4);
    const auto responses = scalar(*fields, 14, 4), triggers = scalar(*fields, 18, 4);
    const auto question = scalar(*fields, 22, 4), atoms = scalar(*fields, 26, 4);
    const auto flag = scalar(*fields, 30, 1);
    if (!name || !icons || !responses || !triggers || !question || !atoms || !flag) {
        return {};
    }
    StoredConversation result;
    result.id = id;
    result.offset = fields->offset;
    result.name_id = *name;
    result.icons_id = *icons;
    result.responses_id = *responses;
    result.triggers_id = *triggers;
    result.question_id = *question;
    result.atoms_id = *atoms;
    result.raw_flag = static_cast<std::uint8_t>(*flag);
    const auto title = parse_stored_fields(bytes, index, 0x2f, result.name_id);
    if (title && !title->blobs.empty()) {
        result.name = title->blobs.front().bytes;
        result.name_available = true;
    }
    return result;
}

std::optional<StoredConversationList>
parse_stored_conversations(std::span<const std::uint8_t> bytes, const OfflineDatabaseIndex& index,
                           std::uint32_t class_id, std::uint32_t id) {
    if (class_id == 0x2d) {
        const auto fields = parse_stored_fields(bytes, index, class_id, id);
        if (!fields) {
            return {};
        }
        const auto list = scalar(*fields, 22, 4);
        if (!list) {
            return {};
        }
        id = *list;
        if (!id) {
            return StoredConversationList{};
        }
    } else if (class_id != 0x32) {
        return {};
    }
    const auto list = parse_stored_object_list(bytes, index, 0x32, id);
    if (!list) {
        return {};
    }
    return StoredConversationList{id, list->ids};
}
}
