#include "devtools/state_variables.h"
#include <commctrl.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
}

int main() {
    HWND parent = nullptr;
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        require(InitCommonControlsEx(&controls), "List controls unavailable");
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1000, 700, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        const auto list =
            CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | LVS_SINGLESEL, 0, 0, 980,
                            680, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent && list, "Cannot create variable list");
        for (int index = 0; index < 7; ++index) {
            LVCOLUMNW column{};
            column.mask = LVCF_WIDTH;
            column.cx = 100;
            ListView_InsertColumn(list, index, &column);
        }
        devtools::GameSnapshot snapshot;
        snapshot.manager = 1;
        snapshot.state = 2;
        snapshot.variables = {
            {{0x53, UINT32_MAX, true},
             L"cOfficeWhereAreWe (P=Phone)",
             L"RegUberVars [1]",
             devtools::DatabaseVariable{80, 0x80}},
            {{0x53, UINT32_MAX, false}, L"Duplicate", L"", devtools::DatabaseVariable{0, 1}},
            {{0x53, 3, true}, L"Duplicate", L"", devtools::DatabaseVariable{-9, 1}}};
        devtools::StateVariableView view;
        devtools::update_state_variables(list, view, snapshot, L"",
                                         devtools::StateVariableFilter::all, false, false);
        require(ListView_GetItemCount(list) == 3, "Table lost cached variables");
        const auto edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 100, 24, parent, nullptr,
                                          GetModuleHandleW(nullptr), nullptr);
        require(edit != nullptr, "Cannot create variable edit control");
        const auto edit_text = [&] {
            std::array<wchar_t, 32> text{};
            GetWindowTextW(edit, text.data(), static_cast<int>(text.size()));
            return std::wstring(text.data());
        };
        devtools::StateVariableEdit draft;
        devtools::update_state_edit_value(edit, draft, snapshot, &snapshot.variables[0]);
        require(edit_text() == L"80", "Selected value did not initialize editor");
        SetWindowTextW(edit, L"81");
        SetFocus(list);
        devtools::update_state_edit_value(edit, draft, snapshot, &snapshot.variables[0]);
        require(edit_text() == L"81", "Focus change or unchanged refresh discarded edit draft");
        SetWindowTextW(edit, L"'A'");
        devtools::update_state_edit_value(edit, draft, snapshot, &snapshot.variables[0]);
        require(edit_text() == L"'A'", "Unchanged refresh discarded character draft");
        auto unnamed_character = snapshot.variables[0];
        unnamed_character.name = L"Letter setting";
        require(devtools::state_variable_text(unnamed_character).find(L"ASCII projection: 'P'") !=
                    std::wstring::npos,
                "Printable ASCII projection requires a special variable name");
        auto editing_snapshot = snapshot;
        editing_snapshot.variables[0].value->raw_value = 82;
        devtools::update_state_edit_value(edit, draft, editing_snapshot,
                                          &editing_snapshot.variables[0]);
        require(edit_text() == L"82", "Changed snapshot retained stale draft");
        SetWindowTextW(edit, L"83");
        editing_snapshot.session = 10;
        devtools::update_state_edit_value(edit, draft, editing_snapshot,
                                          &editing_snapshot.variables[0]);
        require(edit_text() == L"82", "New native session retained edit draft");
        SetWindowTextW(edit, L"83");
        editing_snapshot.variables[0].address = 99;
        devtools::update_state_edit_value(edit, draft, editing_snapshot,
                                          &editing_snapshot.variables[0]);
        require(edit_text() == L"82", "Replaced native object retained edit draft");
        SetWindowTextW(edit, L"83");
        devtools::update_state_edit_value(edit, draft, snapshot, &snapshot.variables[2]);
        require(edit_text() == L"-9", "Selecting another variable retained previous draft");
        devtools::update_state_edit_value(edit, draft, snapshot, nullptr);
        require(edit_text().empty(), "Absent selection retained stale draft text");
        const auto authored = std::find(view.rows.begin(), view.rows.end(), 0u);
        require(authored != view.rows.end(), "Authored variable absent");
        ListView_SetItemState(list, static_cast<int>(authored - view.rows.begin()), LVIS_SELECTED,
                              LVIS_SELECTED);
        const auto selected_key = devtools::selected_state_variable(list, view, snapshot)->key;
        devtools::toggle_state_watch(view, selected_key);
        require(devtools::is_state_watched(view, selected_key),
                "Pin did not retain owned identity");
        auto watch_snapshot = snapshot;
        watch_snapshot.variables[0].address = 1234;
        watch_snapshot.variables[0].name = L"Updated display name";
        std::reverse(watch_snapshot.variables.begin(), watch_snapshot.variables.end());
        view.sort_column = 1;
        view.descending = true;
        devtools::update_state_variables(list, view, watch_snapshot, L"",
                                         devtools::StateVariableFilter::all, false, false, true);
        require(ListView_GetItemCount(list) == 1 &&
                    devtools::selected_state_variable(list, view, watch_snapshot)->key ==
                        selected_key,
                "Watch or selection depended on row order, name or pointer");
        auto other_source = selected_key;
        other_source.state_database = false;
        auto other_class = selected_key;
        other_class.class_id = 0x54;
        require(!devtools::is_state_watched(view, other_source) &&
                    !devtools::is_state_watched(view, other_class),
                "Watch identity omitted database source or native class");
        devtools::reset_state_baseline(view, watch_snapshot);
        require(devtools::is_state_watched(view, selected_key), "Baseline reset removed watches");
        watch_snapshot.variables.push_back(watch_snapshot.variables.back());
        watch_snapshot.variables.back().key.class_id = 0x54;
        devtools::update_state_variables(list, view, watch_snapshot, L"",
                                         devtools::StateVariableFilter::all, false, false, true);
        require(ListView_GetItemCount(list) == 1,
                "Watched filter matched a different native class");
        devtools::toggle_state_watch(view, selected_key);
        devtools::update_state_variables(list, view, watch_snapshot, L"",
                                         devtools::StateVariableFilter::all, false, false, true);
        require(ListView_GetItemCount(list) == 0 && view.watches.empty(),
                "Unpin retained watched row");
        devtools::toggle_state_watch(view, selected_key);
        watch_snapshot.session = 22;
        devtools::update_state_variables(list, view, watch_snapshot, L"",
                                         devtools::StateVariableFilter::all, false, false, true);
        require(ListView_GetItemCount(list) == 0 && view.watches.empty() && !view.selected,
                "Native session change retained watches or selection");
        view.sort_column = 0;
        view.descending = false;
        devtools::update_state_variables(list, view, snapshot, L"",
                                         devtools::StateVariableFilter::all, false, false);
        ListView_SetItemState(
            list,
            static_cast<int>(std::find(view.rows.begin(), view.rows.end(), 0u) - view.rows.begin()),
            LVIS_SELECTED, LVIS_SELECTED);

        snapshot.variables[0].value->raw_value = 65;
        devtools::update_state_variables(list, view, snapshot, L"",
                                         devtools::StateVariableFilter::all, false, true);
        require(ListView_GetItemCount(list) == 1 &&
                    devtools::selected_state_variable(list, view, snapshot)->key == selected_key,
                "Changed filter or full 32-bit selection identity failed");
        const auto row_text = devtools::state_variable_text(snapshot.variables[0]);
        require(row_text.starts_with(L"cOfficeWhereAreWe (P=Phone): 65") &&
                    row_text.find(L"0x00000041") != std::wstring::npos &&
                    row_text.find(L"ASCII projection: 'A'") != std::wstring::npos &&
                    row_text.find(L"State ID 4294967295") != std::wstring::npos,
                "Full literal selected row omitted owned data");
        devtools::update_state_variables(list, view, snapshot, L"phone",
                                         devtools::StateVariableFilter::registered, true, false);
        require(ListView_GetItemCount(list) == 1,
                "Literal name search or registered filter failed");
        devtools::reset_state_baseline(view, snapshot);
        devtools::update_state_variables(list, view, snapshot, L"",
                                         devtools::StateVariableFilter::all, false, true);
        require(ListView_GetItemCount(list) == 0, "Reset baseline retained changed records");
        snapshot.state = 4;
        snapshot.variables[0].value->raw_value = 66;
        devtools::update_state_variables(list, view, snapshot, L"",
                                         devtools::StateVariableFilter::all, false, true);
        require(ListView_GetItemCount(list) == 0 && !view.selected,
                "New state context retained old baseline or selection");
        snapshot.variables[0].value.reset();
        devtools::reset_state_baseline(view, snapshot);
        snapshot.variables[0].value = devtools::DatabaseVariable{67, 0};
        devtools::update_state_variables(list, view, snapshot, L"",
                                         devtools::StateVariableFilter::all, false, true);
        require(ListView_GetItemCount(list) == 0,
                "Unreadable baseline became a claimed value change");
        devtools::reset_state_baseline(view, snapshot);
        snapshot.variables[0].value.reset();
        devtools::update_state_variables(list, view, snapshot, L"",
                                         devtools::StateVariableFilter::all, false, true);
        require(ListView_GetItemCount(list) == 0,
                "Unreadable current value became a claimed value change");
        snapshot.variables[0].value = devtools::DatabaseVariable{67, 0};
        snapshot.variables.push_back(snapshot.variables[0]);
        devtools::reset_state_baseline(view, snapshot);
        snapshot.variables[0].value->raw_value = 68;
        snapshot.variables.pop_back();
        devtools::update_state_variables(list, view, snapshot, L"",
                                         devtools::StateVariableFilter::all, false, true);
        require(ListView_GetItemCount(list) == 0,
                "Ambiguous baseline became a claimed value change");
        devtools::reset_state_baseline(view, snapshot);
        snapshot.variables[0].value->raw_value = 69;
        snapshot.variables.push_back(snapshot.variables[0]);
        devtools::update_state_variables(list, view, snapshot, L"",
                                         devtools::StateVariableFilter::all, false, true);
        require(ListView_GetItemCount(list) == 0,
                "Ambiguous current identity became a claimed value change");
        auto class_snapshot = snapshot;
        class_snapshot.variables.resize(2);
        class_snapshot.variables[1] = class_snapshot.variables[0];
        class_snapshot.variables[1].key.class_id = 0x54;
        devtools::reset_state_baseline(view, class_snapshot);
        ++class_snapshot.variables[0].value->raw_value;
        ++class_snapshot.variables[1].value->raw_value;
        devtools::update_state_variables(list, view, class_snapshot, L"",
                                         devtools::StateVariableFilter::all, false, true);
        require(ListView_GetItemCount(list) == 2,
                "Distinct native classes shared a baseline or ambiguity count");
        snapshot.variables.assign(919, snapshot.variables[0]);
        devtools::update_state_variables(list, view, snapshot, L"",
                                         devtools::StateVariableFilter::all, false, false);
        require(ListView_GetItemCount(list) == 919, "Full cached variable collection was capped");
        DestroyWindow(parent);
        std::cout << "Variable table full coverage, search, baseline and identity passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
