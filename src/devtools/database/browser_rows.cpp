#include "browser_state.h"
#include "browser_stored_list.h"
#include "browser_stored_action.h"
#include "browser_stored_asset_list.h"
#include "browser_stored_asset_ref.h"
#include "browser_stored_fields.h"
#include "game/database/stored_variable.h"
#include "game/database/stored_trigger.h"
#include <algorithm>
#include <map>

namespace devtools::database_browser {
void populate(Browser& state) {
    auto selected_key = state.pending_selection;
    state.pending_selection.reset();
    if (!selected_key) {
        if (const auto* row = selected_row(state)) {
            selected_key = row->key;
        }
    }
    state.rebuilding = true;
    const wchar_t* headings[] = {native_mode(state) || state.mode_index == 7 ? L"ID"
                                 : state.mode_index == 6                     ? L"Asset"
                                                                             : L"Offset",
                                 native_mode(state) || state.mode_index == 7 ? L"Record type"
                                 : state.mode_index == 6                     ? L"Type"
                                                                             : L"Bytes",
                                 native_mode(state)      ? L"Database"
                                 : state.mode_index == 7 ? L"File offset"
                                 : state.mode_index == 6 ? L"Availability"
                                                         : L"Source",
                                 L"Description"};
    for (int column = 0; column < 4; ++column) {
        LVCOLUMNW heading{};
        heading.mask = LVCF_TEXT;
        heading.pszText = const_cast<LPWSTR>(headings[column]);
        ListView_SetColumn(state.list, column, &heading);
    }
    state.rows.clear();
    const auto query = lower(text(state.search));
    std::size_t matched = 0;
    const auto add = [&](Row row) {
        std::wstring searchable = row.search;
        for (const auto& column : row.columns) {
            searchable += L" " + column;
        }
        if (!query.empty() && lower(searchable).find(query) == std::wstring::npos) {
            return;
        }
        ++matched;
        if (state.rows.size() < 200000) {
            state.rows.push_back(std::move(row));
        }
    };
    if (native_mode(state)) {
        const auto cls = filter_class(state);
        for (std::size_t index = 0; index < state.native.objects.size(); ++index) {
            const auto& object = state.native.objects[index];
            if ((state.mode_index >= 4 && !object.variable) || (cls && cls != object.class_id)) {
                continue;
            }
            if (state.mode_index == 5 && std::find(state.changes.begin(), state.changes.end(),
                                                   object.key()) == state.changes.end()) {
                continue;
            }
            add({row_key(object.key()),
                 {std::to_wstring(object.id), std::wstring(database_class_name(object.class_id)),
                  object.state_database ? L"State" : L"HDB", object.description},
                 index,
                 {},
                 object.file_offset,
                 hex(object.id) + L" " + hex(object.class_id) + L" " + object.fields});
        }
    } else if (state.mode_index == 7) {
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> cached_keys;
        for (const auto& object : state.native.objects) {
            if (!object.state_database) {
                ++cached_keys[{object.class_id, object.id}];
            }
        }
        const auto cls = filter_class(state);
        for (std::size_t index = 0; index < state.stored.records.size(); ++index) {
            const auto& record = state.stored.records[index];
            if (cls && cls != record.class_id) {
                continue;
            }
            const auto found = cached_keys.find({record.class_id, record.id});
            const bool cached = found != cached_keys.end() && found->second == 1;
            const auto key = row_key({record.class_id, record.id, false});
            Row row{key,
                    {std::to_wstring(record.id), std::wstring(database_class_name(record.class_id)),
                     record.offset ? hex(record.offset) : L"Unavailable",
                     !state.snapshot_provider ? L"Stored definition"
                     : cached                 ? L"Stored definition | also cached"
                                              : L"Stored definition | uncached"},
                    {},
                    {},
                    record.offset ? std::optional<std::uint64_t>{record.offset} : std::nullopt,
                    hex(record.id) + L" " + hex(record.class_id),
                    index};
            if (state.database) {
                stored_list_row(state, record, row);
                stored_action_row(state, record, row);
                stored_asset_list_row(state, record, row);
                stored_asset_ref_row(state, record, row);
                stored_fields_row(state, record, row);
                if (const auto trigger = game_assets::parse_stored_trigger(
                        state.database->bytes(), state.stored, record.class_id, record.id)) {
                    row.columns[3] += L" | Action list " +
                                      std::to_wstring(trigger->action_list_id) + L" | Event " +
                                      std::to_wstring(trigger->event_type);
                    if (trigger->event_type == 8) {
                        row.columns[3] += L" Object Activation";
                    }
                }
                if (const auto variable = game_assets::parse_stored_variable(
                        state.database->bytes(), state.stored, record.class_id, record.id)) {
                    row.columns[3] = std::wstring(variable->name.begin(), variable->name.end()) +
                                     L" = " + std::to_wstring(variable->signed_value);
                }
            }
            add(std::move(row));
        }
    } else if (state.mode_index == 6) {
        for (std::size_t index = 0; index < state.assets.assets.size(); ++index) {
            const auto& asset = state.assets.assets[index];
            const wchar_t* types[] = {
                L"Movie",   L"Navigation archive", L"Image archive", L"Audio",
                L"Image",   L"Database",           L"Font",          L"Palette",
                L"Hotspot", L"Other asset",        L"Text",          L"Localization"};
            if ((state.filter_index == 1 && !asset.present) ||
                (state.filter_index == 2 && asset.present) ||
                (state.filter_index >= 3 && state.filter_index < 3 + std::size(types) &&
                 asset.type != types[state.filter_index - 3])) {
                continue;
            }
            add({index,
                 {asset.path.generic_wstring(), asset.type,
                  asset.present ? L"Installed" : L"Missing", asset.summary},
                 {},
                 index,
                 {},
                 {}});
        }
    } else if (state.mode_index == 1 && state.clips) {
        for (const auto& clip : state.clips->entries()) {
            for (const auto& label : clip.labels) {
                auto description = label.location + L" / " + label.scene;
                if (label.node) {
                    description += L" / Node " + std::to_wstring(*label.node);
                }
                add({label.offset,
                     {hex(label.offset), L"", clip.movie, std::move(description)},
                     {},
                     database_asset_find(state.assets, clip.movie),
                     label.offset,
                     label.kind + L" " + label.category});
            }
        }
    } else if (state.mode_index == 2 && state.database) {
        for (const auto& string : state.database->strings()) {
            std::wstring summary(string.text.begin(), string.text.end());
            for (auto& ch : summary) {
                if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
                    ch = L' ';
                }
            }
            add({string.offset,
                 {hex(string.offset), std::to_wstring(string.size), L"Database text",
                  std::move(summary)},
                 {},
                 {},
                 string.offset,
                 {}});
        }
    } else if (state.mode_index == 3) {
        for (auto at = state.io.reads.rbegin(); at != state.io.reads.rend(); ++at) {
            add({at->sequence,
                 {hex(at->offset), std::to_wstring(at->size),
                  L"Read #" + std::to_wstring(at->sequence),
                  std::wstring(at->caller_is_rva ? L"Caller RVA " : L"Caller address ") +
                      hex(at->caller)},
                 {},
                 {},
                 at->offset,
                 {}});
        }
    }
    std::stable_sort(state.rows.begin(), state.rows.end(), [&](const Row& a, const Row& b) {
        const auto column = static_cast<std::size_t>(state.sort_column);
        int order = 0;
        if (column == 0 && state.mode_index != 6) {
            const auto first = a.stored   ? state.stored.records[*a.stored].id
                               : a.object ? state.native.objects[*a.object].id
                                          : a.offset.value_or(a.key);
            const auto second = b.stored   ? state.stored.records[*b.stored].id
                                : b.object ? state.native.objects[*b.object].id
                                           : b.offset.value_or(b.key);
            order = first < second ? -1 : first > second ? 1 : 0;
        } else {
            order = lower(a.columns[column]).compare(lower(b.columns[column]));
        }
        return state.sort_descending ? order > 0 : order < 0;
    });
    ListView_SetItemCount(state.list, static_cast<int>(state.rows.size()));
    ListView_SetItemState(state.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (selected_key) {
        for (std::size_t at = 0; at < state.rows.size(); ++at) {
            if (state.rows[at].key == *selected_key) {
                ListView_SetItemState(state.list, static_cast<int>(at),
                                      LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(state.list, static_cast<int>(at), FALSE);
                break;
            }
        }
    }
    state.rebuilding = false;
    const wchar_t* explanations[] = {
        L"Select a record. Open Links to follow outgoing and incoming references.",
        L"Authoring labels. Double-click a label to browse its asset.",
        L"ASCII text fragments with unverified boundaries. Text preserves line breaks. Original "
        L"bytes are on Raw bytes.",
        L"Latest 64 synchronous reads. Selection uses copied file bytes.",
        L"Refresh before and after a game action to compare copied values.",
        L"Value or flag changes since the previous refresh. New variables excluded.",
        L"Installed assets and referenced missing files. Select an asset, then Open preview.",
        L"Stored database records. Selection reads copied file bytes, without loading game "
        L"objects."};
    auto status = std::to_wstring(matched) + L" rows | Read-only file browser";
    if (state.snapshot_provider) {
        status = std::to_wstring(matched) + L" rows | " + std::to_wstring(state.io.total_reads) +
                 L" native reads | " +
                 std::wstring(state.io.open ? L"native file open"
                                            : L"native file closed / not observed");
    }
    if (matched > state.rows.size()) {
        status += L" | First 200000 shown, narrow the search";
    }
    status += L"\r\n" + std::wstring(explanations[state.mode_index]);
    if (state.native.truncated && native_mode(state)) {
        status += L" Snapshot reached its safety limit.";
    }
    if (state.mode_index == 6 && state.assets.truncated) {
        status += L" Asset scan reached its safety limit.";
    }
    if (state.mode_index == 7) {
        status += L" " + state.stored.status;
    }
    SetWindowTextW(state.status, status.c_str());
    sync_database_class(state);
    update_navigation(state);
    EnableWindow(state.filter,
                 native_mode(state) || state.mode_index == 6 || state.mode_index == 7);
    layout(state);
    describe(state);
    InvalidateRect(state.list, nullptr, FALSE);
}

}
