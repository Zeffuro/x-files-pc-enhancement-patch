#include "browser_stored_action.h"
#include "payload.h"
#include "game/database/stored_action.h"
#include <algorithm>
#include <array>
#include <bit>

namespace devtools::database_browser {
namespace {
std::wstring function_name(std::span<const std::uint8_t> bytes, bool raw = false) {
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring text;
    for (const auto byte : bytes) {
        if (raw) {
            text += digits[byte >> 4];
            text += digits[byte & 15];
        } else if (!byte) {
            break;
        } else if (byte == '\\') {
            text += L"\\\\";
        } else if (byte >= 32 && byte <= 126) {
            text += static_cast<wchar_t>(byte);
        } else {
            text += L"\\x";
            text += digits[byte >> 4];
            text += digits[byte & 15];
        }
    }
    return text;
}

DatabaseExpression expression(const game_assets::StoredActionExpression& value) {
    return {
        {value.left.raw, value.left.kind}, {value.right.raw, value.right.kind}, value.operation};
}

struct SetViewReference {
    std::uint32_t id, class_id;
    const wchar_t* label;
};

auto set_view_references(const game_assets::StoredSetViewAction& value) {
    return std::array{SetViewReference{value.view_id, 0x2b, L"view"},
                      SetViewReference{value.node_id, 0x28, L"node"},
                      SetViewReference{value.location_id, 0x29, L"location"},
                      SetViewReference{value.viewpoint_id, 0x2a, L"viewpoint"}};
}

void operand_links(Browser& state, const DatabaseExpression& expression,
                   const std::wstring& label) {
    for (const auto& [operand, side] :
         {std::pair{expression.left, L" left"}, std::pair{expression.right, L" right"}}) {
        if (operand.kind != 0) {
            continue;
        }
        const auto target = game_assets::find_database_record(state.stored, 0x53, operand.raw);
        const bool linked = operand.raw && bool(target);
        state.links.push_back(
            {{L"To", L"Stored " + label + side, L"VCVariable ID " + std::to_wstring(operand.raw),
              !operand.raw ? L"None"
              : linked     ? L"Stored definition"
                           : L"Missing / ambiguous"},
             {linked ? std::optional<DatabaseObjectKey>{{0x53, operand.raw, false}} : std::nullopt,
              {},
              true}});
    }
}
}

void stored_action_row(const Browser& state, const game_assets::StoredDatabaseRecord& record,
                       Row& row) {
    if (!state.database) {
        return;
    }
    const auto action = game_assets::parse_stored_action(state.database->bytes(), state.stored,
                                                         record.class_id, record.id);
    if (!action) {
        return;
    }
    row.columns[3] += L" | Raw action type " + std::to_wstring(action->action_type);
    row.search += L" " + hex(action->resource_mark);
    if (action->predicate) {
        row.search += L" Encoded condition: " +
                      database_expression_text(expression(*action->predicate), true);
    }
    if (action->statement) {
        row.columns[3] +=
            L" | Statement: " + database_expression_text(expression(*action->statement), false);
    }
    if (action->asset) {
        row.columns[3] += L" | Asset-list ID " + std::to_wstring(action->asset->id) +
                          L" | Raw asset selector " + std::to_wstring(action->asset->selector);
        row.search += L" " + hex(action->asset->id);
    }
    if (action->timer) {
        row.columns[3] += L" | Timer duration (raw) " + std::to_wstring(action->timer->duration) +
                          L" | Timer ID " + std::to_wstring(action->timer->id) +
                          L" | Raw timer control " + std::to_wstring(action->timer->control);
        row.search += L" " + hex(action->timer->duration);
    }
    if (action->enable) {
        row.columns[3] += L" | Enable target ID " + std::to_wstring(action->enable->id) +
                          L" | Enable raw word " + std::to_wstring(action->enable->raw_word) +
                          L" | Raw enable control " + std::to_wstring(action->enable->control);
        row.search += L" " + hex(action->enable->id) + L" " + hex(action->enable->raw_word);
    }
    if (action->set_view) {
        for (const auto& reference : set_view_references(*action->set_view)) {
            row.columns[3] += L" | Set View " + std::wstring(reference.label) + L" ID " +
                              std::to_wstring(reference.id);
            row.search += L" " + hex(reference.id);
        }
    }
    if (action->interface_action) {
        row.columns[3] +=
            L" | Interface layout ID " + std::to_wstring(action->interface_action->id) +
            L" | Raw interface control " + std::to_wstring(action->interface_action->control);
        row.search += L" " + hex(action->interface_action->id);
    }
    if (action->function) {
        const auto& function = *action->function;
        row.columns[3] += L" | C++ Function name " + function_name(function.name) +
                          L" | Function argument count " +
                          std::to_wstring(function.arguments.size());
        row.search += L" " + function_name(function.name, true);
        for (std::size_t i = 0; i < function.arguments.size(); ++i) {
            const auto& argument = function.arguments[i];
            row.search += L" Function argument " + std::to_wstring(i) + L" word (raw) " +
                          std::to_wstring(argument.raw) + L" kind (raw) " +
                          std::to_wstring(argument.kind) + L" " + hex(argument.raw);
        }
    }
    if (action->sound) {
        const auto& sound = *action->sound;
        row.columns[3] += L" | 3D Sound asset-list ID " + std::to_wstring(sound.id) +
                          L" | Raw sound selector " + std::to_wstring(sound.selector) +
                          L" | Raw sound byte +5 " + std::to_wstring(sound.raw_byte5);
        row.search += L" " + hex(sound.id);
        for (const auto& [offset, word] : {std::pair{6, sound.word6}, {8, sound.word8}}) {
            row.columns[3] += L" | Sound word +" + std::to_wstring(offset) + L" (raw) " +
                              std::to_wstring(word) + L" (signed) " +
                              std::to_wstring(std::bit_cast<std::int16_t>(word));
            row.search += L" " + hex(word);
        }
    }
}

void stored_action_properties(Browser& state, const game_assets::StoredDatabaseRecord& record,
                              std::wstring& raw) {
    if (!state.database || record.class_id != 0x41) {
        return;
    }
    const auto property = [&](HTREEITEM parent, const std::wstring& text) {
        TVINSERTSTRUCTW node{};
        node.hParent = parent;
        node.hInsertAfter = TVI_LAST;
        node.item.mask = TVIF_TEXT;
        node.item.pszText = const_cast<LPWSTR>(text.c_str());
        return TreeView_InsertItem(state.properties, &node);
    };
    const auto action = game_assets::parse_stored_action(state.database->bytes(), state.stored,
                                                         record.class_id, record.id);
    if (!action) {
        property(TVI_ROOT, L"Stored action decoding unavailable or unsupported.");
        return;
    }
    const auto group = property(TVI_ROOT, L"Stored action");
    property(group, L"Raw type byte: " + std::to_wstring(action->action_type));
    property(group, L"Payload file offset: " + hex(action->resource_mark));
    property(group, L"Payload byte length: " + std::to_wstring(action->encoded_length));
    property(group, L"Decoded descriptor: 15 bytes, stored version 1");
    property(group, L"Encoded definition. Runtime context and execution are not evaluated.");
    if (action->predicate) {
        const auto value = expression(*action->predicate);
        property(group, L"Encoded condition: " + database_expression_text(value, true));
        operand_links(state, value, L"Condition");
    }
    if (action->statement) {
        const auto value = expression(*action->statement);
        property(group, L"Statement: " + database_expression_text(value, false));
        operand_links(state, value, L"Statement");
    } else if (action->asset) {
        const auto& asset = *action->asset;
        property(group, L"Asset-list ID: " + std::to_wstring(asset.id));
        property(group, L"Asset selector (raw): " + std::to_wstring(asset.selector));
        property(group, L"Asset reference class: 0x00000036");
        const auto target = game_assets::find_database_record(state.stored, 0x36, asset.id);
        const bool linked = asset.id && bool(target);
        state.links.push_back(
            {{L"To", L"Stored Asset", L"VCAssetRefList ID " + std::to_wstring(asset.id),
              !asset.id ? L"None"
              : linked  ? L"Stored definition"
                        : L"Missing / ambiguous"},
             {linked ? std::optional<DatabaseObjectKey>{{0x36, asset.id, false}} : std::nullopt,
              {},
              true}});
    } else if (action->timer) {
        const auto& timer = *action->timer;
        property(group, L"Timer duration (raw): " + std::to_wstring(timer.duration));
        property(group, L"Timer ID: " + std::to_wstring(timer.id));
        property(group, L"Timer control byte (raw): " + std::to_wstring(timer.control));
        property(group, L"Timer selector (low 7 bits): " + std::to_wstring(timer.control & 0x7f));
        property(group, L"Timer flag (bit 7): " + std::to_wstring(bool(timer.control & 0x80)));
        property(group, L"Timer ID identifies runtime state. No stored definition link.");
    } else if (action->enable) {
        const auto& enable = *action->enable;
        property(group, L"Enable target ID: " + std::to_wstring(enable.id));
        property(group, L"Enable word at body +4 (raw): " + std::to_wstring(enable.raw_word));
        property(group, L"Enable control byte (raw): " + std::to_wstring(enable.control));
        property(group, L"Enable flag (nonzero): " + std::to_wstring(bool(enable.control)));
        property(group, L"Enable reference class: 0x00000046 (VCEnabled)");
        property(group, L"Enable target uses native database routing. No stored definition link.");
    } else if (action->set_view) {
        property(
            group,
            L"Set View references are stored candidates. Runtime resolution is not evaluated.");
        for (const auto& reference : set_view_references(*action->set_view)) {
            const auto label = L"Set View " + std::wstring(reference.label);
            property(group, label + L" ID: " + std::to_wstring(reference.id));
            property(group, label + L" reference class: " + hex(reference.class_id) + L" (" +
                                std::wstring(database_class_name(reference.class_id)) + L")");
            const auto target =
                game_assets::find_database_record(state.stored, reference.class_id, reference.id);
            const bool linked = reference.id && bool(target);
            state.links.push_back({{L"To", L"Stored " + label,
                                    std::wstring(database_class_name(reference.class_id)) +
                                        L" ID " + std::to_wstring(reference.id),
                                    !reference.id ? L"None"
                                    : linked      ? L"Stored definition"
                                                  : L"Missing / ambiguous"},
                                   {linked ? std::optional<DatabaseObjectKey>{{reference.class_id,
                                                                               reference.id, false}}
                                           : std::nullopt,
                                    {},
                                    true}});
        }
    } else if (action->interface_action) {
        const auto& layout = *action->interface_action;
        property(group, L"Interface layout ID: " + std::to_wstring(layout.id));
        property(group, L"Interface control byte (raw): " + std::to_wstring(layout.control));
        property(group, L"Interface reference class: 0x0000004e (VCIFaceLayout)");
        property(
            group,
            L"Interface reference is a stored candidate. Runtime resolution is not evaluated.");
        const auto target = game_assets::find_database_record(state.stored, 0x4e, layout.id);
        const bool linked = layout.id && bool(target);
        state.links.push_back(
            {{L"To", L"Stored Interface", L"VCIFaceLayout ID " + std::to_wstring(layout.id),
              !layout.id ? L"None"
              : linked   ? L"Stored definition"
                         : L"Missing / ambiguous"},
             {linked ? std::optional<DatabaseObjectKey>{{0x4e, layout.id, false}} : std::nullopt,
              {},
              true}});
    } else if (action->function) {
        const auto& function = *action->function;
        property(group, L"C++ Function name: " + function_name(function.name));
        property(group, L"Function name bytes (raw): " + function_name(function.name, true));
        property(group, L"Function name NUL within 15 bytes: " +
                            std::to_wstring(std::find(function.name.begin(), function.name.end(),
                                                      0) != function.name.end()));
        property(group, L"Function argument count: " + std::to_wstring(function.arguments.size()));
        property(group, L"Function lookup and argument evaluation are not performed.");
        for (std::size_t i = 0; i < function.arguments.size(); ++i) {
            const auto& argument = function.arguments[i];
            const auto label = L"Function argument " + std::to_wstring(i);
            property(group, label + L" word (raw): " + std::to_wstring(argument.raw));
            property(group, label + L" kind (raw): " + std::to_wstring(argument.kind));
        }
    } else if (action->sound) {
        const auto& sound = *action->sound;
        property(group, L"3D Sound asset-list ID: " + std::to_wstring(sound.id));
        property(group, L"3D Sound selector (raw): " + std::to_wstring(sound.selector));
        property(group, L"3D Sound byte at body +5 (raw): " + std::to_wstring(sound.raw_byte5));
        for (const auto& [offset, word] : {std::pair{6, sound.word6}, {8, sound.word8}}) {
            const auto label = L"3D Sound word at body +" + std::to_wstring(offset);
            property(group, label + L" (raw): " + std::to_wstring(word));
            property(group,
                     label + L" (signed): " + std::to_wstring(std::bit_cast<std::int16_t>(word)));
        }
        property(group, L"3D Sound reference class: 0x00000036 (VCAssetRefList)");
        property(group, L"3D Sound reference is a stored candidate. Effects are not evaluated.");
        const auto target = game_assets::find_database_record(state.stored, 0x36, sound.id);
        const bool linked = sound.id && bool(target);
        state.links.push_back(
            {{L"To", L"Stored 3D Sound", L"VCAssetRefList ID " + std::to_wstring(sound.id),
              !sound.id ? L"None"
              : linked  ? L"Stored definition"
                        : L"Missing / ambiguous"},
             {linked ? std::optional<DatabaseObjectKey>{{0x36, sound.id, false}} : std::nullopt,
              {},
              true}});
    } else {
        property(group, L"Action body unsupported or invalid length. Payload remains raw.");
    }
    raw += L"\r\n\r\nStored action payload at " + hex(action->resource_mark) + L"\r\n" +
           database_native_hex(action->payload);
    TreeView_Expand(state.properties, group, TVE_EXPAND);
}
}
