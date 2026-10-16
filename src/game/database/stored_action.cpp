#include "stored_action.h"
#include "stored_format.h"
#include <algorithm>

namespace game_assets {
namespace format = stored_format;

std::optional<StoredAction> parse_stored_action(std::span<const std::uint8_t> bytes,
                                                const OfflineDatabaseIndex& index,
                                                std::uint32_t class_id, std::uint32_t id) {
    if (class_id != format::class_id::action || !id) {
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
    const auto half = [&](std::size_t at) {
        return static_cast<std::uint16_t>((std::uint16_t(bytes[at]) << 8) | bytes[at + 1]);
    };
    if (bytes.size() < format::header_size || bytes.size() > format::max_database_bytes ||
        word(0) != 5 || word(20) != 0x501 || word(24) != 0x40000 || word(28) != 256) {
        return {};
    }
    const auto offset = std::size_t(index.records[*found].offset);
    const auto overlaps_index = [&](std::size_t start, std::size_t end) {
        const auto range =
            std::lower_bound(index.node_ranges.begin(), index.node_ranges.end(), start,
                             [](const auto& range, auto start) { return range.second <= start; });
        return range != index.node_ranges.end() && range->first < end;
    };
    const auto contains_definition = [&](std::size_t start, std::size_t end) {
        const auto at = std::lower_bound(index.definition_offsets.begin(),
                                         index.definition_offsets.end(), start);
        return at != index.definition_offsets.end() && *at < end;
    };
    const auto aliases =
        std::equal_range(index.definition_offsets.begin(), index.definition_offsets.end(), offset);
    if (offset < format::header_size || !has(offset, format::action::descriptor_size) ||
        !(bytes[offset] & format::definition_flag) || word(offset + 2) != 1 ||
        std::distance(aliases.first, aliases.second) != 1 ||
        contains_definition(offset + 1, offset + format::action::descriptor_size) ||
        overlaps_index(offset, offset + format::action::descriptor_size)) {
        return {};
    }
    StoredAction result;
    result.offset = static_cast<std::uint32_t>(offset);
    result.resource_mark = word(offset + 6);
    result.encoded_length = word(offset + 10);
    result.action_type = bytes[offset + 14];
    if (result.encoded_length > 4096) {
        return {};
    }
    if (!result.resource_mark || !result.encoded_length) {
        return !result.resource_mark && !result.encoded_length ? std::optional{result}
                                                               : std::nullopt;
    }
    const auto start = std::size_t(result.resource_mark);
    if (start < format::header_size || !has(start, result.encoded_length)) {
        return {};
    }
    const auto end = start + result.encoded_length;
    if ((start < offset + format::action::descriptor_size && offset < end) ||
        overlaps_index(start, end) || contains_definition(start, end)) {
        return {};
    }
    result.payload.assign(bytes.begin() + start, bytes.begin() + end);
    const auto expression = [&](std::size_t at) {
        return StoredActionExpression{
            {word(at), bytes[at + 8]}, {word(at + 4), bytes[at + 9]}, bytes[at + 10]};
    };
    const std::size_t prefix = (result.action_type & format::action::conditional_flag) ? 12 : 0;
    if (prefix && result.encoded_length >= prefix) {
        result.predicate = expression(start);
    }
    const auto type = result.action_type & format::action::type_mask;
    // The file reader swaps these two words before native operand construction.
    if (type == format::action::statement && result.encoded_length == prefix + 11) {
        result.statement = expression(start + prefix);
    }
    if (type == format::action::asset && result.encoded_length == prefix + 5) {
        result.asset = StoredAssetAction{word(start + prefix), bytes[start + prefix + 4]};
    }
    if (type == format::action::timer && result.encoded_length == prefix + 6) {
        result.timer = StoredTimerAction{word(start + prefix), bytes[start + prefix + 4],
                                         bytes[start + prefix + 5]};
    }
    if (type == format::action::enable && result.encoded_length == prefix + 9) {
        result.enable = StoredEnableAction{word(start + prefix), word(start + prefix + 4),
                                           bytes[start + prefix + 8]};
    }
    if (type == format::action::set_view && result.encoded_length == prefix + 16) {
        result.set_view = StoredSetViewAction{word(start + prefix), word(start + prefix + 4),
                                              word(start + prefix + 8), word(start + prefix + 12)};
    }
    if (type == format::action::interface_action && result.encoded_length == prefix + 5) {
        result.interface_action =
            StoredInterfaceAction{word(start + prefix), bytes[start + prefix + 4]};
    }
    if (type == format::action::function && result.encoded_length >= prefix + 16) {
        const auto body = start + prefix;
        const auto count = std::size_t(bytes[body + 15]);
        if (result.encoded_length == prefix + 16 + 5 * count) {
            StoredFunctionAction function;
            std::copy_n(bytes.begin() + body, function.name.size(), function.name.begin());
            function.arguments.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                function.arguments.push_back(
                    {word(body + 16 + 4 * i), bytes[body + 16 + 4 * count + i]});
            }
            result.function = std::move(function);
        }
    }
    if (type == format::action::sound && result.encoded_length == prefix + 10) {
        const auto body = start + prefix;
        result.sound = StoredSoundAction{word(body), bytes[body + 4], bytes[body + 5],
                                         half(body + 6), half(body + 8)};
    }
    return result;
}
}
