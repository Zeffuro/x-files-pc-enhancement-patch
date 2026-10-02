#include "browser_state.h"
#include "field_guide.h"
#include <map>

namespace devtools::database_browser {
std::uint32_t filter_class(const Browser& state) {
    if (state.mode_index == 6 || !state.filter_index) {
        return 0;
    }
    const auto value = SendMessageW(state.filter, CB_GETITEMDATA, state.filter_index, 0);
    return value == CB_ERR ? 0 : static_cast<std::uint32_t>(value);
}

void update_database_table(Browser& state) {
    state.class_rows.clear();
    state.class_ids.clear();
    std::map<std::uint32_t, std::size_t> counts;
    for (const auto& record : state.stored.records) {
        ++counts[record.class_id];
    }
    for (const auto& [cls, count] : counts) {
        state.class_ids.push_back(cls);
        state.class_rows.push_back({hex(cls), std::wstring(database_class_name(cls)),
                                    std::to_wstring(count), database_class_purpose(cls)});
    }
    ListView_SetItemCount(state.database_classes, static_cast<int>(state.class_rows.size()));
    ListView_SetItemState(state.database_classes, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    EnableWindow(state.database_browse, bool(state.database));
    EnableWindow(state.database_view, bool(state.database));
    sync_database_class(state);
    InvalidateRect(state.database_classes, nullptr, FALSE);
}

void sync_database_class(Browser& state) {
    const auto cls = filter_class(state);
    const bool rebuilding = state.rebuilding;
    state.rebuilding = true;
    int choice = -1;
    for (std::size_t index = 0; cls && index < state.class_ids.size(); ++index) {
        if (state.class_ids[index] == cls) {
            choice = static_cast<int>(index);
            break;
        }
    }
    ListView_SetItemState(state.database_classes, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (choice >= 0) {
        ListView_SetItemState(state.database_classes, choice, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(state.database_classes, choice, FALSE);
    } else {
        const auto info = L"Database file: " + state.path.wstring() + L"\r\n" +
                          std::to_wstring(state.stored.records.size()) + L" indexed records in " +
                          std::to_wstring(state.class_rows.size()) + L" classes. " +
                          state.stored.status +
                          L"\r\nSelect a record type on the left to browse its records."
                          L"\r\nStored fields describe saved definitions. Runtime state and "
                          L"unknown field meanings "
                          L"are identified separately.";
        SetWindowTextW(state.database_info, info.c_str());
    }
    state.rebuilding = rebuilding;
}

void browse_database_class(Browser& state) {
    state.views[state.mode_index] = current_view(state);
    const int choice = ListView_GetNextItem(state.database_classes, -1, LVNI_SELECTED);
    const auto cls = choice >= 0 && std::size_t(choice) < state.class_ids.size()
                         ? state.class_ids[static_cast<std::size_t>(choice)]
                         : 0;
    state.mode_index = 7;
    state.rows.clear();
    state.pending_selection.reset();
    state.manual_offset.reset();
    state.sort_column = 0;
    state.sort_descending = false;
    select_mode(state);
    configure_filter(state);
    for (LRESULT index = 0; cls && index < SendMessageW(state.filter, CB_GETCOUNT, 0, 0); ++index) {
        if (SendMessageW(state.filter, CB_GETITEMDATA, index, 0) == static_cast<LRESULT>(cls)) {
            state.filter_index = static_cast<unsigned>(index);
            SendMessageW(state.filter, CB_SETCURSEL, index, 0);
            break;
        }
    }
    state.rebuilding = true;
    SetWindowTextW(state.search, L"");
    ListView_SetItemState(state.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    state.rebuilding = false;
    populate(state);
    if (!cls) {
        select_pane(state, Pane::overview);
    }
    if (cls && !state.rows.empty()) {
        ListView_SetItemState(state.list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
    layout(state);
    SetFocus(state.list);
}
}
