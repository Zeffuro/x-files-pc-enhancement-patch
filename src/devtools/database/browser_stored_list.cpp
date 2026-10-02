#include "browser_stored_list.h"
#include "game/database/stored_list.h"

namespace devtools::database_browser {
void stored_list_row(const Browser& state, const game_assets::StoredDatabaseRecord& record,
                     Row& row) {
    if (!state.database) {
        return;
    }
    const auto list = game_assets::parse_stored_reference_list(
        state.database->bytes(), state.stored, record.class_id, record.id);
    if (!list) {
        return;
    }
    row.columns[3] += L" | " + std::to_wstring(list->ids.size()) +
                      (list->target_class == 0x41 ? L" Action IDs" : L" Trigger IDs");
    row.search += L" " + hex(list->resource_mark);
    for (const auto id : list->ids) {
        row.search += L" " + std::to_wstring(id) + L" " + hex(id);
    }
}

void stored_list_properties(Browser& state, const game_assets::StoredDatabaseRecord& record) {
    if (!state.database || (record.class_id != 0x42 && record.class_id != 0x52)) {
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
    const auto list = game_assets::parse_stored_reference_list(
        state.database->bytes(), state.stored, record.class_id, record.id);
    if (!list) {
        property(TVI_ROOT, L"Stored reference-list decoding unavailable or unsupported.");
        return;
    }
    const auto group = property(TVI_ROOT, L"Stored reference list");
    property(group, L"Count: " + std::to_wstring(list->ids.size()));
    property(group, L"ID-resource mark: " + hex(list->resource_mark));
    property(group, L"Resource class: " + hex(list->resource_class));
    property(group, L"Resource type (raw): " + hex(list->resource_type));
    property(group, L"Resource owner (raw): " + std::to_wstring(list->resource_owner));
    property(group, L"Needs-release byte (raw): " + std::to_wstring(list->needs_release_raw));
    property(group, L"Duplicate byte (raw): " + std::to_wstring(list->duplicate_raw));
    property(group, L"Resource ID (raw): " + std::to_wstring(list->resource_id));
    property(group, L"Decoded descriptor: 32 bytes, stored version 1");
    property(group, L"Resource nodes: " + std::to_wstring(list->resource_nodes));
    property(group, L"Decoded resource bytes: " + std::to_wstring(list->decoded_resource_bytes));
    const auto label = list->target_class == 0x41 ? L"Action" : L"Trigger";
    const auto members = property(group, std::wstring(label) + L" IDs in stored order");
    for (std::size_t i = 0; i < list->ids.size(); ++i) {
        const auto id = list->ids[i];
        const auto field = std::wstring(label) + L"[" + std::to_wstring(i) + L"]";
        property(members, field + L" ID: " + std::to_wstring(id));
        const auto target = game_assets::find_database_record(state.stored, list->target_class, id);
        const bool linked = id && bool(target);
        state.links.push_back(
            {{L"To", L"Stored " + field,
              std::wstring(database_class_name(list->target_class)) + L" ID " + std::to_wstring(id),
              !id      ? L"None"
              : linked ? L"Stored definition"
                       : L"Missing / ambiguous"},
             {linked ? std::optional<DatabaseObjectKey>{{list->target_class, id, false}}
                     : std::nullopt,
              {},
              true}});
    }
    TreeView_Expand(state.properties, members, TVE_EXPAND);
    TreeView_Expand(state.properties, group, TVE_EXPAND);
}
}
