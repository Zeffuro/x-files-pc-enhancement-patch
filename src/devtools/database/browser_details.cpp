#include "browser_state.h"
#include "browser_stored_list.h"
#include "browser_stored_action.h"
#include "browser_stored_asset_list.h"
#include "browser_stored_asset_ref.h"
#include "browser_stored_fields.h"
#include "field_guide.h"
#include "game/database/stored_variable.h"
#include "game/database/stored_trigger.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <map>

namespace devtools::database_browser {
namespace {
HTREEITEM item(Browser& state, HTREEITEM parent, const std::wstring& text) {
    TVINSERTSTRUCTW insert{};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT;
    insert.item.pszText = const_cast<LPWSTR>(text.c_str());
    return TreeView_InsertItem(state.properties, &insert);
}

HTREEITEM group(Browser& state, const std::wstring& name) {
    return item(state, TVI_ROOT, name);
}

void expand(Browser& state, HTREEITEM group) {
    TreeView_Expand(state.properties, group, TVE_EXPAND);
}

std::wstring target_name(const NativeDatabaseObject& object) {
    return std::wstring(database_class_name(object.class_id)) + L" ID " +
           std::to_wstring(object.id) + (object.state_database ? L" (State)" : L" (HDB)");
}

std::wstring property_path(HWND tree, HTREEITEM node, const std::wstring& parent) {
    std::wstring text(8192, L'\0');
    TVITEMW value{};
    value.mask = TVIF_TEXT;
    value.hItem = node;
    value.pszText = text.data();
    value.cchTextMax = static_cast<int>(text.size());
    TreeView_GetItem(tree, &value);
    text.resize(wcslen(text.c_str()));
    text.resize(text.find(L':') == std::wstring::npos ? text.size() : text.find(L':'));
    return parent + L"/" + text;
}

struct TreeState {
    std::map<std::wstring, bool> expanded;
    std::wstring selected;
};

void visit_properties(HWND tree, HTREEITEM node, const std::wstring& parent, TreeState& saved,
                      bool restore) {
    for (; node; node = TreeView_GetNextSibling(tree, node)) {
        const auto path = property_path(tree, node, parent);
        if (restore) {
            if (const auto found = saved.expanded.find(path); found != saved.expanded.end()) {
                TreeView_Expand(tree, node, found->second ? TVE_EXPAND : TVE_COLLAPSE);
            }
            if (saved.selected == path) {
                TreeView_SelectItem(tree, node);
            }
        } else {
            saved.expanded[path] =
                (TreeView_GetItemState(tree, node, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
            if (TreeView_GetSelection(tree) == node) {
                saved.selected = path;
            }
        }
        visit_properties(tree, TreeView_GetChild(tree, node), path, saved, restore);
    }
}

void asset_link(Browser& state, std::size_t index) {
    const auto& asset = state.assets.assets[index];
    state.links.push_back({{L"Asset", L"Media file", asset.path.generic_wstring(),
                            asset.present ? L"Installed" : L"Missing"},
                           {{}, asset.path}});
}

void asset_properties(Browser& state, std::size_t index) {
    const auto& asset = state.assets.assets[index];
    const auto file = group(state, L"Asset file");
    item(state, file, L"Path: " + asset.path.generic_wstring());
    item(state, file, L"Type: " + asset.type);
    item(state, file,
         L"Availability: " +
             std::wstring(asset.present ? L"Installed" : L"Referenced, file missing"));
    if (asset.present) {
        item(state, file, L"Installed location: " + asset.physical_path.wstring());
    }
    const bool archive = lower(asset.path.extension().wstring()) == L".pff";
    item(state, file,
         L"Preview: " + std::wstring(asset.type == L"Text"   ? L"Read text to view this asset"
                                     : asset.type == L"Font" ? L"Show font to view a sample"
                                     : archive ? L"Open archive to browse and export its images"
                                     : asset.type == L"Database"
                                         ? L"Open database to browse stored records. Record types "
                                           L"and their purposes stay visible on the left."
                                     : !asset.previewable
                                         ? L"This format is not supported by the preview library"
                                     : !asset.present ? L"File is missing"
                                                      : L"Open preview to view this asset"));
    expand(state, file);
    if (!asset.labels.empty()) {
        const auto labels =
            group(state, L"Authoring labels (" + std::to_wstring(asset.labels.size()) + L")");
        for (const auto& label : asset.labels) {
            const auto entry = item(state, labels, label.location + L" / " + label.scene);
            if (!label.kind.empty()) {
                item(state, entry, L"Kind: " + label.kind);
            }
            if (!label.category.empty()) {
                item(state, entry, L"Category: " + label.category);
            }
            if (label.node) {
                item(state, entry, L"Node: " + std::to_wstring(*label.node));
            }
            item(state, entry, L"HDB label offset: " + hex(label.offset));
        }
        expand(state, labels);
    }
}

void hotspot_properties(Browser& state, const DatabaseAsset& asset, std::wstring& raw) {
    const auto hotspots = database_asset_hotspots(state.root, asset);
    const auto geometry = group(state, L"Hotspot geometry");
    item(state, geometry, L"Status: " + hotspots.status);
    item(state, geometry, L"File size: " + std::to_wstring(hotspots.file_size) + L" bytes");
    if (hotspots.valid) {
        item(state, geometry, L"Format: HSPT, little endian, 20 bytes per entry");
        item(state, geometry, L"Coordinates: signed 16-bit values on a 640 x 480 canvas");
        item(state, geometry, L"Entries: " + std::to_wstring(hotspots.entries.size()));
        item(state, geometry, L"Action IDs are raw file values. Runtime effects are unresolved.");
        constexpr std::size_t display_limit = 256;
        const auto shown = std::min(hotspots.entries.size(), display_limit);
        for (std::size_t at = 0; at < shown; ++at) {
            const auto& value = hotspots.entries[at];
            const auto rectangle = L"(" + std::to_wstring(value.x_min) + L", " +
                                   std::to_wstring(value.y_min) + L") to (" +
                                   std::to_wstring(value.x_max) + L", " +
                                   std::to_wstring(value.y_max) + L")";
            const auto entry =
                item(state, geometry, L"Entry " + std::to_wstring(at + 1) + L": " + rectangle);
            item(state, entry, L"File offset: " + hex(8 + at * 20));
            item(state, entry,
                 L"Type / z-order (raw): " + std::to_wstring(value.type_zorder) + L" (" +
                     hex(value.type_zorder) + L")");
            item(state, entry, L"Rectangle: " + rectangle);
            item(state, entry,
                 L"Action ID 1 (raw): " + std::to_wstring(value.action_id_1) + L" (" +
                     hex(value.action_id_1) + L")");
            item(state, entry,
                 L"Action ID 2 (raw): " + std::to_wstring(value.action_id_2) + L" (" +
                     hex(value.action_id_2) + L")");
            if (!value.on_canvas()) {
                item(state, entry,
                     value.ordered() ? L"Rectangle extends outside the canvas"
                                     : L"Rectangle coordinates are reversed");
            }
        }
        if (shown < hotspots.entries.size()) {
            item(state, geometry, L"First 256 entries shown (display safety limit)");
        }
    }
    expand(state, geometry);
    raw = L"Hotspot file bytes (little endian)\r\n" + database_native_hex(hotspots.raw);
    if (hotspots.raw.size() < hotspots.file_size) {
        raw += L"\r\nFirst " + std::to_wstring(hotspots.raw.size()) + L" bytes shown.";
    }
    if (hotspots.raw.empty()) {
        raw += L"\r\n" + hotspots.status;
    }
}
}

void describe(Browser& state) {
    state.rebuilding = true;
    const auto* row = selected_row(state);
    const bool asset_selected = row && row->asset;
    if (asset_selected) {
        state.manual_offset.reset();
    }
    const auto selection =
        row ? std::optional(std::pair{state.mode_index, row->key}) : std::nullopt;
    const bool selection_changed = selection != state.described;
    const int previous_link = selection == state.described
                                  ? ListView_GetNextItem(state.relations, -1, LVNI_SELECTED)
                                  : -1;
    TreeState tree;
    if (selection && selection == state.described) {
        visit_properties(state.properties, TreeView_GetRoot(state.properties), L"", tree, false);
    }
    TreeView_DeleteAllItems(state.properties);
    state.described = selection;
    if (selection_changed) {
        state.resource_strings_path.reset();
        state.resource_strings = {};
        filter_strings(state);
    }
    state.links.clear();
    SetWindowTextW(state.content, L"");
    if (selection_changed || !asset_selected || state.assets.assets[*row->asset].type != L"Font") {
        state.font_preview->clear();
    }
    EnableWindow(state.follow, FALSE);
    EnableWindow(state.back, !state.history.empty());
    EnableWindow(state.preview, FALSE);
    SetWindowTextW(state.preview, L"Open preview");
    EnableWindow(state.jump, state.database.has_value() && !asset_selected);
    EnableWindow(state.offset, !asset_selected);
    std::wstring title = state.mode_index == 6 ? L"Select an asset file" : L"Select a record", raw;
    if (row && row->object && *row->object < state.native.objects.size()) {
        const auto& object = state.native.objects[*row->object];
        title = target_name(object) + L"\r\n" + object.description;
        const auto properties = group(state, L"Decoded fields");
        auto fields = object.fields;
        const auto payload_at = fields.find(L"\r\nCopied payload bytes:");
        if (payload_at != std::wstring::npos) {
            raw = fields.substr(payload_at + 2) + L"\r\n\r\n";
            fields.resize(payload_at);
        }
        std::wistringstream lines(fields);
        std::wstring line;
        auto field_group = properties;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                if (line.starts_with(L"Copied runtime parameters.")) {
                    field_group = item(state, properties, L"Runtime parameters");
                    item(state, field_group, L"Copied values depend on dispatch context");
                } else {
                    item(state, field_group, line);
                }
            }
        }
        if (fields.empty()) {
            item(state, properties,
                 object.description.empty() ? L"No decoded fields available" : object.description);
        }
        if (object.variable) {
            const auto before = database_find(state.previous, object.key());
            if (before && state.previous.objects[*before].variable &&
                std::find(state.changes.begin(), state.changes.end(), object.key()) !=
                    state.changes.end()) {
                const auto& old = *state.previous.objects[*before].variable;
                item(state, properties,
                     L"Previous refresh: value " + std::to_wstring(old.raw_value) + L" / flags " +
                         std::to_wstring(old.type_flags));
            }
        }
        expand(state, properties);
        const auto identity = group(state, L"Record identity");
        item(state, identity, L"ID: " + std::to_wstring(object.id));
        item(state, identity,
             L"Class: " + std::wstring(database_class_name(object.class_id)) + L" (" +
                 hex(object.class_id) + L")");
        item(state, identity,
             L"Database: " + std::wstring(object.state_database ? L"State" : L"HDB"));
        item(state, identity, L"Address: " + hex(object.address));
        item(state, identity, L"Vtable RVA: " + hex(object.vtable_rva));
        item(state, identity, L"References: " + std::to_wstring(object.refcount));
        if (object.file_offset) {
            item(state, identity, L"Stored HDB offset: " + hex(*object.file_offset));
        }
        for (const auto& reference : object.relationships) {
            const auto target = database_resolve(state.native, reference);
            const auto matches = std::count_if(
                state.native.objects.begin(), state.native.objects.end(),
                [&](const auto& candidate) {
                    return candidate.class_id == reference.class_id && candidate.id == reference.id;
                });
            const auto stored = !target && !matches && reference.id
                                    ? game_assets::find_database_record(
                                          state.stored, reference.class_id, reference.id)
                                    : std::nullopt;
            state.links.push_back(
                {{L"To", reference.field,
                  std::wstring(database_class_name(reference.class_id)) + L" ID " +
                      std::to_wstring(reference.id),
                  !reference.id ? L"None"
                  : target      ? L"Cached"
                  : stored      ? L"Stored definition"
                                : L"Uncached / ambiguous"},
                 {target ? std::optional<DatabaseObjectKey>{state.native.objects[*target].key()}
                  : stored
                      ? std::optional<DatabaseObjectKey>{{reference.class_id, reference.id, false}}
                      : std::nullopt,
                  {},
                  bool(stored)}});
        }
        if (!object.state_database &&
            game_assets::find_database_record(state.stored, object.class_id, object.id)) {
            state.links.push_back(
                {{L"To", L"Stored definition", target_name(object), L"Stored definition"},
                 {object.key(), {}, true}});
        }
        for (const auto& link : database_links(state.native, *row->object)) {
            if (link.label.starts_with(L"From ")) {
                const auto separator = link.label.find(L" / ");
                state.links.push_back(
                    {{L"From",
                      separator != std::wstring::npos ? link.label.substr(separator + 3)
                                                      : L"Reference",
                      target_name(state.native.objects[link.target]), L"Cached"},
                     {state.native.objects[link.target].key(), {}}});
            }
        }
        if (object.class_id == 0x35) {
            if (const auto asset = database_asset_find(state.assets, object.description)) {
                asset_link(state, *asset);
                asset_properties(state, *asset);
            }
        }
        raw += L"Copied native fields (little endian)\r\n" + database_native_hex(object.bytes);
        if (object.file_offset && state.database && *object.file_offset < state.database->size()) {
            raw += L"\r\nStored HDB bytes at " + hex(*object.file_offset) +
                   L" (live fields may differ)\r\n" + state.database->hex(*object.file_offset);
        }
    } else if (row && row->stored) {
        const auto& record = state.stored.records[*row->stored];
        const DatabaseObjectKey key{record.class_id, record.id, false};
        title = std::wstring(database_class_name(record.class_id)) + L" ID " +
                std::to_wstring(record.id) + L"\r\nStored database record";
        const auto fields = group(state, L"Stored record");
        item(state, fields,
             L"Class: " + std::wstring(database_class_name(record.class_id)) + L" (" +
                 hex(record.class_id) + L")");
        item(state, fields, L"ID: " + std::to_wstring(record.id));
        item(state, fields,
             L"File offset: " + (record.offset ? hex(record.offset) : L"Unavailable"));
        item(
            state, fields,
            L"Serialized record from this file. Current runtime values require a cached instance.");
        item(state, fields, L"Raw bytes show a bounded window, not a verified record length.");
        if (state.database) {
            stored_list_properties(state, record);
            stored_action_properties(state, record, raw);
            stored_asset_list_properties(state, record, raw);
            stored_asset_ref_properties(state, record, raw);
            stored_fields_properties(state, record, raw);
            if (const auto trigger = game_assets::parse_stored_trigger(
                    state.database->bytes(), state.stored, record.class_id, record.id)) {
                const auto decoded = group(state, L"Stored trigger");
                item(state, decoded,
                     L"Action-list ID: " + std::to_wstring(trigger->action_list_id));
                item(state, decoded, L"Event type (raw): " + std::to_wstring(trigger->event_type));
                if (trigger->event_type == 8) {
                    item(state, decoded, L"Event: Object Activation");
                }
                item(state, decoded, L"Decoded extent: 11 bytes, stored version 1");
                expand(state, decoded);
                const auto target =
                    game_assets::find_database_record(state.stored, 0x42, trigger->action_list_id);
                const bool linked = trigger->action_list_id && bool(target);
                state.links.push_back(
                    {{L"To", L"Stored action list",
                      L"VCActionList ID " + std::to_wstring(trigger->action_list_id),
                      !trigger->action_list_id ? L"None"
                      : linked                 ? L"Stored definition"
                                               : L"Missing / ambiguous"},
                     {linked
                          ? std::optional<DatabaseObjectKey>{{0x42, trigger->action_list_id, false}}
                          : std::nullopt,
                      {},
                      true}});
            } else if (record.class_id == 0x51) {
                item(state, fields, L"Stored trigger decoding unavailable or unsupported.");
            }
            if (const auto variable = game_assets::parse_stored_variable(
                    state.database->bytes(), state.stored, record.class_id, record.id)) {
                const auto decoded = group(state, L"Stored variable");
                item(state, decoded,
                     L"Name: " + std::wstring(variable->name.begin(), variable->name.end()));
                item(state, decoded,
                     L"Value (signed 32-bit): " + std::to_wstring(variable->signed_value));
                item(state, decoded, L"Value bits: " + hex(variable->value_bits));
                item(state, decoded, L"Owner ID (raw): " + std::to_wstring(variable->owner_id));
                item(state, decoded, L"Flag (raw): " + std::to_wstring(variable->flag));
                item(state, decoded, L"Type (raw): " + std::to_wstring(variable->type));
                item(state, decoded, L"Name file offset: " + hex(variable->name_offset));
                expand(state, decoded);
            }
        }
        expand(state, fields);
        if (const auto cached = database_find(state.native, key)) {
            state.links.push_back(
                {{L"To", L"Cached instance", target_name(state.native.objects[*cached]), L"Cached"},
                 {key, {}}});
        }
        for (const auto& object : state.native.objects) {
            for (const auto& reference : object.relationships) {
                if (reference.class_id == record.class_id && reference.id == record.id) {
                    state.links.push_back(
                        {{L"From", reference.field, target_name(object), L"Cached"},
                         {object.key(), {}}});
                }
            }
        }
        if (record.offset && state.database) {
            raw = L"Stored database bytes at " + hex(record.offset) + L"\r\n" +
                  state.database->hex(record.offset) + raw;
        }
    } else if (row && row->asset) {
        const auto& asset = state.assets.assets[*row->asset];
        title = asset.path.generic_wstring() + L"\r\n" + asset.type + L" | " +
                (asset.present ? L"Installed" : L"Missing") + L" | " +
                std::to_wstring(asset.labels.size()) + L" labels";
        if (state.snapshot_provider) {
            title += L" | " + std::to_wstring(asset.references.size()) + L" cached references";
        }
        asset_properties(state, *row->asset);
        if (state.mode_index != 6) {
            asset_link(state, *row->asset);
        }
        for (const auto& key : asset.references) {
            if (const auto target = database_find(state.native, key)) {
                state.links.push_back({{L"From", L"Asset path",
                                        target_name(state.native.objects[*target]), L"Cached"},
                                       {key, {}}});
            }
        }
        const bool archive = lower(asset.path.extension().wstring()) == L".pff";
        const bool text_asset = asset.type == L"Text";
        const bool font_asset = asset.type == L"Font";
        const bool database_asset =
            asset.type == L"Database" && ((state.database && lower(asset.physical_path.wstring()) ==
                                                                 lower(state.path.wstring())) ||
                                          (!state.snapshot_provider && bool(state.open_asset)));
        SetWindowTextW(state.preview, archive          ? L"Open archive"
                                      : text_asset     ? L"Read text"
                                      : font_asset     ? L"Show font"
                                      : database_asset ? L"Open database"
                                                       : L"Open preview");
        EnableWindow(state.preview,
                     asset.present && (archive || text_asset || font_asset || database_asset ||
                                       (asset.previewable && bool(state.open_asset))));
        asset_content(state, asset, raw);
        if (font_asset) {
            const auto file = database_asset_file(state.root, asset.path);
            if (file) {
                state.font_preview->load(*file);
            } else {
                state.font_preview->clear();
            }
            item(state, TVI_ROOT, state.font_preview->description());
            InvalidateRect(state.font_view, nullptr, TRUE);
        }
        if (asset.type == L"Hotspot") {
            std::wstring hotspot_raw;
            hotspot_properties(state, asset, hotspot_raw);
        }
        for (const auto sibling : database_asset_siblings(state.assets, *row->asset)) {
            const auto& related = state.assets.assets[sibling];
            state.links.push_back(
                {{L"Asset", L"Same folder and filename stem", related.path.generic_wstring(),
                  related.present ? L"Installed" : L"Missing"},
                 {{}, related.path}});
        }
    } else if (row) {
        title = row->columns[2] + L"\r\n" + row->columns[3];
        if (state.mode_index == 2) {
            title = L"Unverified text fragment\r\n" + hex(row->key);
            text_candidate_content(state, *row);
        }
        const auto fields = group(state, L"File record");
        for (std::size_t column = 0; column < row->columns.size(); ++column) {
            if (!row->columns[column].empty()) {
                item(state, fields, row->columns[column]);
            }
        }
        expand(state, fields);
        if (row->offset && state.database && *row->offset < state.database->size()) {
            raw = L"Database bytes at " + hex(*row->offset) + L"\r\n" +
                  state.database->hex(static_cast<std::size_t>(*row->offset));
        }
    } else if (state.mode_index == 6) {
        title = L"Asset files\r\nSelect a file to inspect or preview";
        const auto info = group(state, L"Asset folder");
        item(state, info, L"Folder: " + state.root.wstring());
        item(state, info, L"Files in catalog: " + std::to_wstring(state.assets.assets.size()));
        item(state, info, L"Matching files: " + std::to_wstring(state.rows.size()));
        item(state, info, L"Search filenames, paths and authoring labels, or choose a file type.");
        item(state, info, L"Select a file, then use its preview button or content tabs.");
        item(state, info, L"Select Database tables or a table on the left to inspect records.");
        expand(state, info);
    } else {
        title = L"Database browser\r\nChoose a view and select a record";
        const auto info =
            group(state, native_mode(state) ? L"Cached database snapshot" : L"File information");
        if (native_mode(state)) {
            item(state, info,
                 state.native.available
                     ? L"Cached nodes inspected: " + std::to_wstring(state.native.visited_nodes)
                     : L"Native database unavailable. Start a game with a supported executable.");
            if (state.native.available) {
                item(state, info, L"Manager address: " + hex(state.native.manager_address));
                item(state, info,
                     L"Unsupported or unreadable nodes: " +
                         std::to_wstring(state.native.skipped_nodes));
            }
        }
        item(state, info,
             L"Database file: " + (state.path.empty() ? L"Unavailable" : state.path.wstring()));
        if (state.mode_index == 7) {
            item(state, info, state.stored.status);
        }
        item(state, info,
             state.snapshot_provider
                 ? L"Browse copied fields and cached links. Use Refresh for current values."
                 : L"Browse stored definitions, installed assets and authoring labels.");
        item(state, info,
             L"Ctrl+C copies the selected property. Double-click a link or press Enter to follow.");
        expand(state, info);
        if (state.database) {
            const auto header = group(state, L"Header words (big endian)");
            for (std::size_t word = 0; word < state.database->header().size(); ++word) {
                item(state, header, hex(word * 4) + L": " + hex(state.database->header()[word]));
            }
            raw = state.database->hex(0);
        } else {
            raw = L"File data unavailable. Use Refresh to retry.";
        }
    }
    if (state.manual_offset && state.database && !asset_selected) {
        raw = state.database->hex(*state.manual_offset);
    }
    if (raw.empty()) {
        raw = L"No raw bytes available for this selection.";
    }
    SetWindowTextW(state.title, title.c_str());
    SetWindowTextW(state.detail, raw.c_str());
    update_field_table(state);
    ListView_SetItemCount(state.relations, static_cast<int>(state.links.size()));
    ListView_SetItemState(state.relations, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    const int choice =
        previous_link >= 0 && std::size_t(previous_link) < state.links.size() ? previous_link : 0;
    if (!state.links.empty()) {
        ListView_SetItemState(state.relations, choice, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        const auto& destination = state.links[static_cast<std::size_t>(choice)].destination;
        EnableWindow(state.follow, destination.object || !destination.asset.empty());
    }
    visit_properties(state.properties, TreeView_GetRoot(state.properties), L"", tree, true);
    state.rebuilding = false;
    configure_tabs(state);
    InvalidateRect(state.relations, nullptr, FALSE);
}

void copy_property(Browser& state) {
    std::wstring buffer(8192, L'\0');
    TVITEMW item{};
    item.mask = TVIF_TEXT;
    item.hItem = TreeView_GetSelection(state.properties);
    item.pszText = buffer.data();
    item.cchTextMax = static_cast<int>(std::size(buffer));
    if (!item.hItem || !TreeView_GetItem(state.properties, &item) || !OpenClipboard(state.window)) {
        return;
    }
    const auto bytes = (wcslen(buffer.c_str()) + 1) * sizeof(wchar_t);
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        if (auto* target = GlobalLock(memory)) {
            std::memcpy(target, buffer.data(), bytes);
            GlobalUnlock(memory);
            if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) {
                GlobalFree(memory);
            }
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}
}
