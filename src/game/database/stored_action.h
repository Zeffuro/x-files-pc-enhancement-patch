#pragma once
#include "offline_index.h"
#include <array>

namespace game_assets {
struct StoredActionOperand {
    std::uint32_t raw = 0;
    std::uint8_t kind = 0;
    bool operator==(const StoredActionOperand&) const = default;
};

struct StoredActionExpression {
    StoredActionOperand left, right;
    std::uint8_t operation = 0;
    bool operator==(const StoredActionExpression&) const = default;
};

struct StoredAssetAction {
    std::uint32_t id = 0;
    std::uint8_t selector = 0;
    bool operator==(const StoredAssetAction&) const = default;
};

struct StoredTimerAction {
    std::uint32_t duration = 0;
    std::uint8_t id = 0, control = 0;
    bool operator==(const StoredTimerAction&) const = default;
};

struct StoredEnableAction {
    std::uint32_t id = 0, raw_word = 0;
    std::uint8_t control = 0;
    bool operator==(const StoredEnableAction&) const = default;
};

struct StoredSetViewAction {
    std::uint32_t view_id = 0, node_id = 0, location_id = 0, viewpoint_id = 0;
    bool operator==(const StoredSetViewAction&) const = default;
};

struct StoredInterfaceAction {
    std::uint32_t id = 0;
    std::uint8_t control = 0;
    bool operator==(const StoredInterfaceAction&) const = default;
};

struct StoredFunctionAction {
    std::array<std::uint8_t, 15> name{};
    std::vector<StoredActionOperand> arguments;
    bool operator==(const StoredFunctionAction&) const = default;
};

struct StoredSoundAction {
    std::uint32_t id = 0;
    std::uint8_t selector = 0, raw_byte5 = 0;
    std::uint16_t word6 = 0, word8 = 0;
    bool operator==(const StoredSoundAction&) const = default;
};

struct StoredAction {
    std::uint32_t offset = 0, resource_mark = 0, encoded_length = 0;
    std::uint8_t action_type = 0;
    std::vector<std::uint8_t> payload;
    std::optional<StoredActionExpression> predicate, statement;
    std::optional<StoredAssetAction> asset;
    std::optional<StoredTimerAction> timer;
    std::optional<StoredEnableAction> enable;
    std::optional<StoredSetViewAction> set_view;
    std::optional<StoredInterfaceAction> interface_action;
    std::optional<StoredFunctionAction> function;
    std::optional<StoredSoundAction> sound;
};

std::optional<StoredAction> parse_stored_action(std::span<const std::uint8_t> bytes,
                                                const OfflineDatabaseIndex& index,
                                                std::uint32_t class_id, std::uint32_t id);
}
