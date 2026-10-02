#include "browser_stored_fields.h"
#include "game/database/stored_fields.h"
#include <bit>

namespace devtools::database_browser {
namespace {
std::optional<std::int16_t> signed_projection(std::uint32_t cls,
                                              const game_assets::StoredScalarField& field) {
    const bool rectangle = cls == 0x37 || cls == 0x39 || cls == 0x4f;
    if (field.width == 2 && ((rectangle && field.offset >= 10 && field.offset <= 16) ||
                             cls == 0x3d || (cls == 0x4f && field.offset == 28) ||
                             (cls == 0x2a && (field.offset == 32 || field.offset == 34)) ||
                             (cls == 0x2d && field.offset >= 14 && field.offset <= 20) ||
                             (cls == 0x59 && field.offset >= 22 && field.offset <= 28) ||
                             (cls == 0x57 && field.offset == 130))) {
        return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(field.value));
    }
    return {};
}

std::wstring blob_label(std::uint32_t cls, std::size_t index) {
    return std::wstring(cls >= 0x56 ? L"Resource[" : L"String[") + std::to_wstring(index) + L"]";
}

std::wstring escaped(std::span<const std::uint8_t> bytes) {
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring text;
    for (const auto byte : bytes) {
        if (byte >= 0x20 && byte <= 0x7e && byte != '\\') {
            text += wchar_t(byte);
        } else {
            text += L"\\x";
            text += digits[byte >> 4];
            text += digits[byte & 15];
        }
    }
    return text;
}
}

void stored_fields_row(const Browser& state, const game_assets::StoredDatabaseRecord& record,
                       Row& row) {
    if (!state.database) {
        return;
    }
    const auto fields = game_assets::parse_stored_fields(state.database->bytes(), state.stored,
                                                         record.class_id, record.id);
    if (!fields) {
        return;
    }
    row.columns[3] += L" | Decoded fields: " + std::to_wstring(fields->extent) + L" bytes";
    row.search += L" Flags (raw): " + std::to_wstring(fields->flags[0]) + L", " +
                  std::to_wstring(fields->flags[1]);
    for (const auto& field : fields->scalars) {
        row.search += L" Raw field +" + std::to_wstring(field.offset) + L": " +
                      std::to_wstring(field.value) + L" " + hex(field.value);
        if (const auto projected = signed_projection(record.class_id, field)) {
            row.search += L" Field +" + std::to_wstring(field.offset) +
                          L" signed 16-bit projection: " + std::to_wstring(*projected);
        }
    }
    for (std::size_t i = 0; i < fields->blobs.size(); ++i) {
        const auto& blob = fields->blobs[i];
        row.search += L" " + blob_label(record.class_id, i) + L" data: " + escaped(blob.bytes) +
                      L" " + hex(blob.mark) + L" " + std::to_wstring(blob.bytes.size());
        for (std::size_t word = 0; word < blob.little_endian_words.size(); ++word) {
            row.search += L" LE32[" + std::to_wstring(word) + L"] raw word: " +
                          std::to_wstring(blob.little_endian_words[word]);
        }
    }
    if (fields->list) {
        row.columns[3] +=
            L" | " + std::to_wstring(fields->list->ids.size()) + L" Ordered member IDs";
        for (std::size_t i = 0; i < fields->list->ids.size(); ++i) {
            row.search += L" Member[" + std::to_wstring(i) + L"] ID: " +
                          std::to_wstring(fields->list->ids[i]) + L" " + hex(fields->list->ids[i]);
        }
        row.search += L" Resource nodes: " + std::to_wstring(fields->list->nodes.size()) +
                      L" Decoded resource bytes: " +
                      std::to_wstring(fields->list->decoded_resource_bytes);
    }
}

void stored_fields_properties(Browser& state, const game_assets::StoredDatabaseRecord& record,
                              std::wstring& raw) {
    if (!state.database || !game_assets::supports_stored_fields(record.class_id)) {
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
    const auto fields = game_assets::parse_stored_fields(state.database->bytes(), state.stored,
                                                         record.class_id, record.id);
    if (!fields) {
        property(TVI_ROOT, L"Stored field decoding unavailable or unsupported.");
        return;
    }
    const auto group = property(
        TVI_ROOT, L"Stored " + std::wstring(database_class_name(record.class_id)) + L" definition");
    property(group, L"Flags (raw): " + std::to_wstring(fields->flags[0]) + L", " +
                        std::to_wstring(fields->flags[1]));
    for (const auto& field : fields->scalars) {
        property(group, L"Field +" + std::to_wstring(field.offset) + L" (raw, " +
                            std::to_wstring(field.width) + L" bytes): " +
                            std::to_wstring(field.value) + L" (" + hex(field.value) + L")");
        if (const auto signed_value = signed_projection(record.class_id, field)) {
            property(group, L"Field +" + std::to_wstring(field.offset) +
                                L" signed 16-bit projection: " + std::to_wstring(*signed_value));
        }
    }
    for (std::size_t i = 0; i < fields->blobs.size(); ++i) {
        const auto& blob = fields->blobs[i];
        const auto label = blob_label(record.class_id, i);
        property(group, label + L" mark: " + hex(blob.mark));
        property(group, label + L" size: " + std::to_wstring(blob.bytes.size()));
        property(
            group,
            label + L" data (escaped): " +
                escaped(
                    std::span(blob.bytes).first(std::min(blob.bytes.size(), std::size_t{256}))));
        if (blob.bytes.size() > 256) {
            property(group, L"Preview shows first 256 bytes. Search and Raw retain all bytes.");
        }
        if (blob.word_array) {
            const auto words = property(group, L"Raw little-endian words in stored order");
            for (std::size_t word = 0; word < blob.little_endian_words.size(); ++word) {
                property(words, L"LE32[" + std::to_wstring(word) + L"] raw word: " +
                                    std::to_wstring(blob.little_endian_words[word]));
            }
            property(words, L"Trailing raw bytes: " + std::to_wstring(blob.bytes.size() % 4));
        }
        for (std::size_t at = 0; at < blob.bytes.size(); at += 4096) {
            raw += L"\r\nStored " + label + L" data at " + hex(blob.mark + at) + L"\r\n" +
                   state.database->hex(blob.mark + at,
                                       std::min(blob.bytes.size() - at, std::size_t{4096}));
        }
    }
    if (fields->list) {
        const auto& list = *fields->list;
        const auto members = property(group, L"Member IDs in stored order");
        for (std::size_t i = 0; i < list.ids.size(); ++i) {
            property(members,
                     L"Member[" + std::to_wstring(i) + L"] ID: " + std::to_wstring(list.ids[i]));
        }
        property(group, L"Resource nodes: " + std::to_wstring(list.nodes.size()));
        property(group, L"Decoded resource bytes: " + std::to_wstring(list.decoded_resource_bytes));
        for (const auto& node : list.nodes) {
            raw += L"\r\nStored reference-list resource node at " + hex(node.offset) + L"\r\n" +
                   state.database->hex(node.offset, node.extent);
        }
        TreeView_Expand(state.properties, members, TVE_EXPAND);
    }
    if (record.class_id == 0x46) {
        const auto id = fields->scalars[0].value;
        const auto target_class = fields->scalars[2].value;
        property(group, L"Target ID: " + std::to_wstring(id));
        property(group, L"Target query class: " + hex(target_class));
        const bool linked =
            id && bool(game_assets::find_database_record(state.stored, target_class, id));
        state.links.push_back(
            {{L"To", L"Stored enabled target",
              std::wstring(database_class_name(target_class)) + L" " + hex(target_class) + L" ID " +
                  std::to_wstring(id),
              !id      ? L"None"
              : linked ? L"Stored definition"
                       : L"Missing / ambiguous"},
             {linked ? std::optional<DatabaseObjectKey>{{target_class, id, false}} : std::nullopt,
              {},
              true}});
    }
    property(group, L"Decoded descriptor: " + std::to_wstring(fields->extent) +
                        L" bytes, stored version 1");
    raw += L"\r\nStored field descriptor at " + hex(fields->offset) + L"\r\n" +
           state.database->hex(fields->offset, fields->extent);
    TreeView_Expand(state.properties, group, TVE_EXPAND);
}
}
