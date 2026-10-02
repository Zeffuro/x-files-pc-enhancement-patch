#include "browser_stored_asset_list.h"
#include "game/database/stored_asset_list.h"

namespace devtools::database_browser {
void stored_asset_list_row(const Browser& state, const game_assets::StoredDatabaseRecord& record,
                           Row& row) {
    if (!state.database || (record.class_id >= 0x27 && record.class_id <= 0x2a)) {
        return;
    }
    const auto list = game_assets::parse_stored_object_list(state.database->bytes(), state.stored,
                                                            record.class_id, record.id);
    if (!list) {
        return;
    }
    const bool asset = record.class_id == 0x36;
    if (record.class_id == 0x4e) {
        row.search += L" Raw word +32: " + std::to_wstring(list->trailing_word) + L" " +
                      hex(list->trailing_word) + L" Raw byte +36: " +
                      std::to_wstring(list->trailing_byte);
    }
    row.columns[3] += L" | " + std::to_wstring(list->ids.size()) +
                      (asset ? L" Asset-reference IDs" : L" Ordered member IDs");
    for (const auto value : {list->resource_mark, list->resource_class, list->resource_type,
                             list->resource_owner, list->resource_id}) {
        row.search += L" " + std::to_wstring(value) + L" " + hex(value);
    }
    row.search += L" Flags (raw): " + std::to_wstring(list->flags[0]) + L", " +
                  std::to_wstring(list->flags[1]) + L" Resource nodes: " +
                  std::to_wstring(list->nodes.size()) + L" Decoded resource bytes: " +
                  std::to_wstring(list->decoded_resource_bytes) + L" Raw needs-release byte " +
                  std::to_wstring(list->needs_release_raw) + L" Raw duplicate byte " +
                  std::to_wstring(list->duplicate_raw);
    for (std::size_t i = 0; i < list->ids.size(); ++i) {
        row.search += std::wstring(asset ? L" Asset[" : L" Member[") + std::to_wstring(i) +
                      L"] ID: " + std::to_wstring(list->ids[i]) + L" " + hex(list->ids[i]);
    }
}

void stored_asset_list_properties(Browser& state, const game_assets::StoredDatabaseRecord& record,
                                  std::wstring& raw) {
    if (!state.database || (record.class_id >= 0x27 && record.class_id <= 0x2a)) {
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
    const auto list = game_assets::parse_stored_object_list(state.database->bytes(), state.stored,
                                                            record.class_id, record.id);
    if (!list) {
        switch (record.class_id) {
            case 0x2e:
            case 0x32:
            case 0x34:
            case 0x36:
            case 0x38:
            case 0x3a:
            case 0x3b:
            case 0x4d:
            case 0x4e:
            case 0x55:
                property(TVI_ROOT,
                         record.class_id == 0x36
                             ? L"Stored asset-reference list decoding unavailable or unsupported."
                             : L"Stored reference-list decoding unavailable or unsupported.");
        }
        return;
    }
    const bool asset = record.class_id == 0x36;
    const auto group =
        property(TVI_ROOT, asset ? L"Stored asset-reference list"
                                 : L"Stored " + std::wstring(database_class_name(record.class_id)) +
                                       L" reference list");
    property(group, L"Count: " + std::to_wstring(list->ids.size()));
    property(group, L"Flags (raw): " + std::to_wstring(list->flags[0]) + L", " +
                        std::to_wstring(list->flags[1]));
    property(group, L"ID-resource mark: " + hex(list->resource_mark));
    property(group, L"Resource class: " + hex(list->resource_class));
    property(group, L"Resource type (raw): " + hex(list->resource_type));
    property(group, L"Descriptor word +22 (raw): " + std::to_wstring(list->resource_owner));
    property(group, L"Needs-release byte (raw): " + std::to_wstring(list->needs_release_raw));
    property(group, L"Duplicate byte (raw): " + std::to_wstring(list->duplicate_raw));
    property(group, L"Resource ID (raw): " + std::to_wstring(list->resource_id));
    property(group, L"Decoded descriptor: " + std::to_wstring(list->descriptor_extent) +
                        L" bytes, stored version 1");
    if (record.class_id == 0x4e) {
        property(group, L"Word +32 (raw): " + std::to_wstring(list->trailing_word) + L" (" +
                            hex(list->trailing_word) + L")");
        property(group, L"Byte +36 (raw): " + std::to_wstring(list->trailing_byte));
    }
    property(group, L"Resource nodes: " + std::to_wstring(list->nodes.size()));
    property(group, L"Decoded resource bytes: " + std::to_wstring(list->decoded_resource_bytes));
    const auto members = property(group, asset ? L"Asset-reference IDs in stored order"
                                               : L"Member IDs in stored order");
    for (std::size_t i = 0; i < list->ids.size(); ++i) {
        property(members, std::wstring(asset ? L"Asset[" : L"Member[") + std::to_wstring(i) +
                              L"] ID: " + std::to_wstring(list->ids[i]));
    }
    raw += std::wstring(asset ? L"\r\nStored asset-reference list descriptor at "
                              : L"\r\nStored reference-list descriptor at ") +
           hex(list->offset) + L"\r\n" + state.database->hex(list->offset, list->descriptor_extent);
    for (const auto& node : list->nodes) {
        raw += std::wstring(asset ? L"\r\nStored asset-reference resource node at "
                                  : L"\r\nStored reference-list resource node at ") +
               hex(node.offset) + L"\r\n" + state.database->hex(node.offset, node.extent);
    }
    TreeView_Expand(state.properties, members, TVE_EXPAND);
    TreeView_Expand(state.properties, group, TVE_EXPAND);
}
}
