#include "inspector_internal.h"
#include "state_capture.h"
#include <algorithm>
#include <cstring>

namespace devtools::inspector {
namespace {
bool checked(HWND control) {
    return SendMessageW(control, BM_GETCHECK, 0, 0) != 0;
}

bool history_matches(const StateHistoryEntry& entry, int filter, const StateVariable* selected) {
    if (filter == 1) {
        return selected && entry.key && *entry.key == selected->key;
    }
    if (filter == 2) {
        return entry.key && is_state_watched(state.state_variable_view, *entry.key);
    }
    return true;
}
}

void create_state_controls() {
    state.refresh = child(L"BUTTON", L"Live updates", BS_AUTOCHECKBOX | WS_TABSTOP, refresh_id);
    SendMessageW(state.refresh, BM_SETCHECK, BST_CHECKED, 0);
    state.state_snapshot =
        child(L"BUTTON", L"Refresh snapshot", BS_PUSHBUTTON | WS_TABSTOP, state_snapshot_id);
    state.state_search =
        child(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, state_search_id);
    SendMessageW(state.state_search, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Search state name or value"));
    state.hotspots =
        child(L"BUTTON", L"Show interaction targets", BS_AUTOCHECKBOX | WS_TABSTOP, hotspots_id);
    state.state_text = child(WC_TREEVIEWW, L"Game state",
                             WS_BORDER | WS_TABSTOP | TVS_HASBUTTONS | TVS_LINESATROOT |
                                 TVS_SHOWSELALWAYS | TVS_FULLROWSELECT);
    TreeView_SetBkColor(state.state_text, GetSysColor(COLOR_BTNFACE));
    state.state_filter = child(L"COMBOBOX", L"Variable group",
                               CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, state_filter_id);
    for (const auto* name : {L"All cached variables", L"WhereAreWe names", L"Registered variables",
                             L"State variables", L"HDB variables"}) {
        SendMessageW(state.state_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    SendMessageW(state.state_filter, CB_SETCURSEL, 0, 0);
    state.state_nonzero =
        child(L"BUTTON", L"Nonzero only", BS_AUTOCHECKBOX | WS_TABSTOP, state_nonzero_id);
    state.state_changed =
        child(L"BUTTON", L"Changed only", BS_AUTOCHECKBOX | WS_TABSTOP, state_changed_id);
    state.state_watched =
        child(L"BUTTON", L"Watched only", BS_AUTOCHECKBOX | WS_TABSTOP, state_watched_id);
    state.state_baseline =
        child(L"BUTTON", L"Reset baseline", BS_PUSHBUTTON | WS_TABSTOP, state_baseline_id);
    state.state_watch =
        child(L"BUTTON", L"Pin variable", BS_PUSHBUTTON | WS_TABSTOP, state_watch_id);
    state.state_edit = child(L"BUTTON", L"Enable editing (this session)",
                             BS_AUTOCHECKBOX | WS_TABSTOP, state_edit_id);
    state.state_capture = child(L"BUTTON", L"Capture history (this session)",
                                BS_AUTOCHECKBOX | WS_TABSTOP, state_capture_id);
    state.state_variables =
        child(WC_LISTVIEWW, L"Cached state variables",
              WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
              state_variables_id);
    ListView_SetExtendedListViewStyle(state.state_variables,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    int state_column = 0;
    for (const auto* name : {L"Literal native name", L"Value", L"Baseline", L"Type", L"Source",
                             L"ID", L"Registration"}) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<LPWSTR>(name);
        column.cx = state_column == 0 ? 390 : state_column == 6 ? 190 : 110;
        ListView_InsertColumn(state.state_variables, state_column++, &column);
    }
    state.state_detail =
        child(L"EDIT", L"",
              WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP);
    state.state_value =
        child(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, state_value_id);
    SendMessageW(state.state_value, EM_SETLIMITTEXT, 32, 0);
    SendMessageW(state.state_value, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Decimal or 'A'"));
    state.state_apply =
        child(L"BUTTON", L"Apply value", BS_PUSHBUTTON | WS_TABSTOP, state_apply_id);
    EnableWindow(state.state_value, FALSE);
    EnableWindow(state.state_apply, FALSE);
    EnableWindow(state.state_watch, FALSE);
    state.state_history_filter =
        child(L"COMBOBOX", L"History filter", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
              state_history_filter_id);
    for (const auto* name : {L"History: all variables", L"History: selected variable",
                             L"History: watched variables"}) {
        SendMessageW(state.state_history_filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    SendMessageW(state.state_history_filter, CB_SETCURSEL, 0, 0);
    state.state_history_clear =
        child(L"BUTTON", L"Clear history", BS_PUSHBUTTON | WS_TABSTOP, state_history_clear_id);
    state.state_history_copy =
        child(L"BUTTON", L"Copy history", BS_PUSHBUTTON | WS_TABSTOP, state_history_copy_id);
    state.state_history_status =
        child(L"EDIT", L"History capture is off.",
              WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP);
    state.state_history_list =
        child(WC_LISTVIEWW, L"Variable change history",
              WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
    ListView_SetExtendedListViewStyle(state.state_history_list,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    int history_column = 0;
    for (const auto* name :
         {L"Tick ms", L"Variable / source / ID", L"Old", L"New", L"Observation", L"Caller"}) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<LPWSTR>(name);
        column.cx = history_column == 1                          ? 340
                    : history_column == 4 || history_column == 5 ? 150
                    : history_column >= 2                        ? 65
                                                                 : 130;
        ListView_InsertColumn(state.state_history_list, history_column++, &column);
    }
}

void layout_state_controls(int width, int height) {
    const int left = 252, span = width - left - 20;
    MoveWindow(state.refresh, 20, 66, 130, 30, TRUE);
    MoveWindow(state.state_snapshot, 156, 66, 140, 30, TRUE);
    MoveWindow(state.hotspots, 308, 66, 220, 30, TRUE);
    MoveWindow(state.state_search, 540, 66, std::max(80, width - 560), 30, TRUE);
    MoveWindow(state.state_filter, 20, 106, 220, 250, TRUE);
    MoveWindow(state.state_nonzero, 252, 106, 120, 28, TRUE);
    MoveWindow(state.state_changed, 380, 106, 130, 28, TRUE);
    MoveWindow(state.state_watched, 518, 106, 130, 28, TRUE);
    MoveWindow(state.state_baseline, 660, 106, 140, 28, TRUE);
    MoveWindow(state.state_watch, 20, 142, 220, 28, TRUE);
    MoveWindow(state.state_edit, left, 142, 250, 28, TRUE);
    MoveWindow(state.state_capture, left + 270, 142, 260, 28, TRUE);
    MoveWindow(state.state_text, 20, 180, 220, height - 234, TRUE);
    MoveWindow(state.state_variables, left, 180, span, std::max(140, height - 562), TRUE);
    MoveWindow(state.state_detail, left, height - 370, span, 64, TRUE);
    MoveWindow(state.state_value, left, height - 298, 160, 30, TRUE);
    MoveWindow(state.state_apply, left + 172, height - 298, 140, 30, TRUE);
    MoveWindow(state.state_history_filter, left, height - 260, 270, 200, TRUE);
    MoveWindow(state.state_history_clear, left + 282, height - 260, 120, 28, TRUE);
    MoveWindow(state.state_history_copy, left + 414, height - 260, 120, 28, TRUE);
    MoveWindow(state.state_history_list, left, height - 226, span, 110, TRUE);
    MoveWindow(state.state_history_status, left, height - 108, span, 64, TRUE);
    const std::array history_widths{98, std::max(180, span - 500), 52, 52, 124, 152};
    for (int column = 0; column < static_cast<int>(history_widths.size()); ++column) {
        ListView_SetColumnWidth(state.state_history_list, column, history_widths[column]);
    }
}

void show_state_controls(bool visible) {
    for (auto window : {state.refresh,
                        state.hotspots,
                        state.state_text,
                        state.state_search,
                        state.state_snapshot,
                        state.state_filter,
                        state.state_nonzero,
                        state.state_changed,
                        state.state_baseline,
                        state.state_edit,
                        state.state_value,
                        state.state_apply,
                        state.state_variables,
                        state.state_detail,
                        state.state_watch,
                        state.state_watched,
                        state.state_capture,
                        state.state_history_filter,
                        state.state_history_clear,
                        state.state_history_copy,
                        state.state_history_list,
                        state.state_history_status}) {
        ShowWindow(window, visible ? SW_SHOW : SW_HIDE);
    }
}

void update_game_state_view() {
    const auto& view = state.state_variable_view;
    const bool new_context = view.manager != state.snapshot.manager ||
                             view.state != state.snapshot.state || view.hdb != state.snapshot.hdb ||
                             view.application != state.snapshot.application ||
                             view.session != state.snapshot.session;
    if (new_context) {
        SendMessageW(state.state_edit, BM_SETCHECK, BST_UNCHECKED, 0);
    }
    state.rebuilding = true;
    update_state_tree(state.state_text, state.snapshot);
    update_state_variables(
        state.state_variables, state.state_variable_view, state.snapshot, text(state.state_search),
        static_cast<StateVariableFilter>(SendMessageW(state.state_filter, CB_GETCURSEL, 0, 0)),
        checked(state.state_nonzero), checked(state.state_changed), checked(state.state_watched));
    state.rebuilding = false;
    update_state_selection();
}

void update_state_selection() {
    const auto* variable =
        selected_state_variable(state.state_variables, state.state_variable_view, state.snapshot);
    set_text(state.state_detail, std::to_wstring(state.state_variable_view.rows.size()) + L" of " +
                                     std::to_wstring(state.snapshot.variables.size()) +
                                     L" cached variables / " +
                                     std::to_wstring(state.state_variable_view.watches.size()) +
                                     L" watches (cleared on session change)\r\n" +
                                     (variable ? state_variable_text(*variable)
                                               : L"Select a variable. Names are literal native "
                                                 L"text. Reset baseline starts a new comparison."));
    const bool editable = variable && variable->key.state_database && variable->value &&
                          (variable->value->type_flags & 0x7f) <= 2;
    EnableWindow(state.state_value,
                 checked(state.state_edit) && !checked(state.refresh) && editable);
    EnableWindow(state.state_apply,
                 checked(state.state_edit) && !checked(state.refresh) && editable);
    EnableWindow(state.state_watch, variable != nullptr);
    set_text(state.state_watch,
             variable && is_state_watched(state.state_variable_view, variable->key)
                 ? L"Unpin variable"
                 : L"Pin variable");
    update_state_edit_value(state.state_value, state.state_variable_edit, state.snapshot, variable);
    update_state_history_view();
}

void update_state_history_view() {
    const auto* selected =
        selected_state_variable(state.state_variables, state.state_variable_view, state.snapshot);
    const int filter =
        static_cast<int>(SendMessageW(state.state_history_filter, CB_GETCURSEL, 0, 0));
    const auto key = selected ? std::optional(selected->key) : std::nullopt;
    const bool capture = state_capture_enabled();
    const auto coverage = state_capture_status();
    if (state.history_revision == state.state_history.revision && state.history_filter == filter &&
        state.history_selected == key &&
        state.history_watches == state.state_variable_view.watches &&
        state.history_capture == capture && state.history_coverage == coverage) {
        return;
    }
    state.history_revision = state.state_history.revision;
    state.history_filter = filter;
    state.history_selected = key;
    state.history_watches = state.state_variable_view.watches;
    state.history_capture = capture;
    state.history_coverage = coverage;
    std::vector<std::array<std::wstring, 6>> cells;
    state.history_rows.clear();
    for (std::size_t index = 0; index < state.state_history.entries.size(); ++index) {
        const auto& entry = state.state_history.entries[index];
        if (!history_matches(entry, filter, selected)) {
            continue;
        }
        auto label = entry.name.empty() ? L"Unknown variable" : entry.name;
        if (entry.key) {
            label += entry.key->state_database ? L" / State / " : L" / HDB / ";
            label += std::to_wstring(entry.key->id);
        }
        cells.push_back({std::to_wstring(entry.ticks), label, std::to_wstring(entry.before),
                         std::to_wstring(entry.after), state_history_origin(entry),
                         state_history_caller(entry)});
        state.history_rows.push_back(index);
    }
    if (cells != state.history_cells) {
        const int top = ListView_GetTopIndex(state.state_history_list);
        const bool at_end = top + ListView_GetCountPerPage(state.state_history_list) >=
                            ListView_GetItemCount(state.state_history_list);
        state.history_cells = std::move(cells);
        state.rebuilding = true;
        SendMessageW(state.state_history_list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(state.state_history_list);
        for (std::size_t index = 0; index < state.history_cells.size(); ++index) {
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.lParam = static_cast<LPARAM>(
                state.state_history.entries[state.history_rows[index]].sequence);
            item.iItem = static_cast<int>(index);
            item.pszText = state.history_cells[index][0].data();
            const int row = ListView_InsertItem(state.state_history_list, &item);
            if (state.history_selected_sequence &&
                *state.history_selected_sequence ==
                    state.state_history.entries[state.history_rows[index]].sequence) {
                ListView_SetItemState(state.state_history_list, row, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
            }
            for (int field = 1; field < 6; ++field) {
                ListView_SetItemText(state.state_history_list, row, field,
                                     state.history_cells[index][field].data());
            }
        }
        if (!state.history_cells.empty()) {
            const int restore =
                at_end ? static_cast<int>(state.history_cells.size()) - 1
                       : std::min(top, static_cast<int>(state.history_cells.size()) - 1);
            ListView_EnsureVisible(state.state_history_list, restore, FALSE);
        }
        SendMessageW(state.state_history_list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(state.state_history_list, nullptr, FALSE);
        state.rebuilding = false;
    }
    SendMessageW(state.state_capture, BM_SETCHECK,
                 state_capture_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    update_state_history_selection();
    EnableWindow(state.state_history_copy, !state.history_rows.empty());
    EnableWindow(state.state_history_clear,
                 !state.state_history.entries.empty() || state.state_history.dropped != 0);
}

void update_state_history_selection() {
    const int selected = ListView_GetNextItem(state.state_history_list, -1, LVNI_SELECTED);
    std::wstring detail = std::wstring(state_capture_enabled() ? L"Capturing" : L"Capture off") +
                          L" / " + std::to_wstring(state.history_rows.size()) + L" shown / " +
                          std::to_wstring(state.state_history.entries.size()) + L" retained / " +
                          std::to_wstring(state.state_history.dropped) + L" dropped.\r\n";
    if (selected >= 0 && static_cast<std::size_t>(selected) < state.history_rows.size()) {
        const auto& entry = state.state_history.entries[state.history_rows[selected]];
        state.history_selected_sequence = entry.sequence;
        detail += state_history_origin(entry) + L" / " + entry.name + L" / " +
                  std::to_wstring(entry.before) + L" -> " + std::to_wstring(entry.after) + L" / " +
                  state_history_caller(entry) + L"\r\n" + entry.context;
    } else {
        detail += L"Select an entry for writer context. Sampled changes are unattributed and may "
                  L"miss intermediate writes. Ctrl+C copies the selected entry.";
    }
    detail += L"\r\n" + state.history_coverage;
    set_text(state.state_history_status, detail);
}

void copy_state_history(bool selected_only) {
    update_state_history_view();
    const int selected = ListView_GetNextItem(state.state_history_list, -1, LVNI_SELECTED);
    if (selected_only && selected < 0) {
        return;
    }
    std::wstring value =
        L"Sequence\tTick ms\tVariable\tSource class:ID\tChange\tObservation\tCaller\tContext\r\n";
    for (const auto index : state.history_rows) {
        if (selected_only && state.history_selected_sequence &&
            *state.history_selected_sequence != state.state_history.entries[index].sequence) {
            continue;
        }
        value += state_history_text(state.state_history.entries[index]) + L"\r\n";
    }
    const auto bytes = (value.size() + 1) * sizeof(wchar_t);
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        return;
    }
    auto* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return;
    }
    std::memcpy(target, value.c_str(), bytes);
    GlobalUnlock(memory);
    if (OpenClipboard(state.window)) {
        EmptyClipboard();
        const bool transferred = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        CloseClipboard();
        if (transferred) {
            return;
        }
    }
    GlobalFree(memory);
}

bool state_command(int id, int event) {
    if (id == refresh_id || id == state_snapshot_id) {
        state.snapshot = inspect_game();
        state.state_updated = GetTickCount64();
        collect_state_history(state.state_history, state.snapshot, true);
        update_game_state_view();
        return true;
    }
    if ((id == state_search_id && event == EN_CHANGE) ||
        (id == state_filter_id && event == CBN_SELCHANGE) || id == state_nonzero_id ||
        id == state_changed_id || id == state_edit_id || id == state_baseline_id ||
        id == state_watched_id || id == state_watch_id) {
        if (id == state_baseline_id) {
            reset_state_baseline(state.state_variable_view, state.snapshot);
        }
        if (id == state_watch_id) {
            const auto* variable = selected_state_variable(
                state.state_variables, state.state_variable_view, state.snapshot);
            if (variable) {
                toggle_state_watch(state.state_variable_view, variable->key);
            }
        }
        update_game_state_view();
        return true;
    }
    if (id == state_capture_id) {
        const auto result = set_state_capture(checked(state.state_capture));
        if (result.empty() && state_capture_enabled()) {
            auto fresh = inspect_game();
            const bool held = !checked(state.refresh);
            const bool same_context = state.snapshot.manager == fresh.manager &&
                                      state.snapshot.state == fresh.state &&
                                      state.snapshot.hdb == fresh.hdb &&
                                      state.snapshot.application == fresh.application &&
                                      state.snapshot.session == fresh.session;
            if (held && !same_context) {
                set_state_capture(false);
                set_text(state.status,
                         L"Game session changed. Refresh the snapshot before capturing.");
            } else {
                collect_state_history(state.state_history, fresh, true);
                if (!held) {
                    state.snapshot = std::move(fresh);
                    state.state_updated = GetTickCount64();
                    update_game_state_view();
                }
            }
        }
        update_state_history_view();
        if (!result.empty()) {
            set_text(state.status, result);
        }
        return true;
    }
    if ((id == state_history_filter_id && event == CBN_SELCHANGE) || id == state_history_clear_id ||
        id == state_history_copy_id) {
        if (id == state_history_clear_id) {
            collect_state_history(state.state_history, state.snapshot);
            clear_state_history(state.state_history);
            state.history_selected_sequence.reset();
        }
        if (id == state_history_copy_id) {
            copy_state_history();
        }
        update_state_history_view();
        return true;
    }
    if (id != state_apply_id || checked(state.refresh)) {
        return false;
    }
    if (state.prompting || GetWindowThreadProcessId(state.game, nullptr) != GetCurrentThreadId()) {
        set_text(state.status, L"Editing requires the active game thread.");
        return true;
    }
    const auto* variable =
        selected_state_variable(state.state_variables, state.state_variable_view, state.snapshot);
    if (variable) {
        const auto result = edit_game_variable(state.snapshot, *variable, text(state.state_value),
                                               checked(state.state_edit));
        if (result.empty()) {
            state.snapshot = inspect_game();
            state.state_updated = GetTickCount64();
            collect_state_history(state.state_history, state.snapshot, true);
            update_game_state_view();
        }
        set_text(state.status,
                 result.empty() ? L"Native value updated. The snapshot was refreshed." : result);
    }
    return true;
}

bool state_notify(const NMLISTVIEW& change) {
    if (change.hdr.hwndFrom == state.state_history_list) {
        if (change.hdr.code == LVN_ITEMCHANGED) {
            state.history_selected_sequence.reset();
            update_state_history_selection();
        }
        return true;
    }
    if (change.hdr.hwndFrom != state.state_variables) {
        return false;
    }
    if (change.hdr.code == LVN_ITEMCHANGED) {
        update_state_selection();
    }
    if (change.hdr.code == LVN_COLUMNCLICK) {
        if (state.state_variable_view.sort_column == change.iSubItem) {
            state.state_variable_view.descending = !state.state_variable_view.descending;
        } else {
            state.state_variable_view.sort_column = change.iSubItem;
            state.state_variable_view.descending = false;
        }
        update_game_state_view();
    }
    return true;
}
}
