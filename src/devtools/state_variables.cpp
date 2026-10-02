#include "state_variables.h"
#include <commctrl.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace devtools {
namespace {
std::wstring hex(std::uint32_t value) {
    std::wostringstream text;
    text << L"0x" << std::hex << std::setfill(L'0') << std::setw(8) << value;
    return text.str();
}

std::wstring fold(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

const StateVariable* previous(const StateVariableView& view, const StateVariable& variable) {
    const auto key =
        std::make_tuple(variable.key.state_database, variable.key.class_id, variable.key.id);
    const auto count = view.current_counts.find(key);
    const auto found = view.baseline_index.find(key);
    return count == view.current_counts.end() || count->second != 1 ||
                   found == view.baseline_index.end() || found->second >= view.baseline.size()
               ? nullptr
               : &view.baseline[found->second];
}

std::wstring column(const StateVariable& value, int index, const StateVariable* before) {
    switch (index) {
        case 0:
            return value.name;
        case 1:
            return value.value ? std::to_wstring(value.value->raw_value) : L"Unavailable";
        case 2:
            return before && before->value ? std::to_wstring(before->value->raw_value) : L"";
        case 3:
            return state_variable_type(value);
        case 4:
            return value.key.state_database ? L"State" : L"HDB";
        case 5:
            return std::to_wstring(value.key.id);
        case 6:
            return value.registration;
        default:
            return L"";
    }
}
}

void update_state_edit_value(HWND edit, StateVariableEdit& draft, const GameSnapshot& snapshot,
                             const StateVariable* variable) {
    const std::array context{snapshot.manager, snapshot.state, snapshot.hdb,  snapshot.application,
                             snapshot.session, snapshot.view,  snapshot.input};
    const bool same =
        variable && draft.variable && draft.context == context &&
        draft.variable->key == variable->key && draft.variable->address == variable->address &&
        draft.variable->name == variable->name && draft.variable->value == variable->value;
    if (!same) {
        const auto value =
            variable && variable->value ? std::to_wstring(variable->value->raw_value) : L"";
        SetWindowTextW(edit, value.c_str());
    }
    draft.variable = variable ? std::optional(*variable) : std::nullopt;
    draft.context = context;
}

std::wstring state_variable_type(const StateVariable& variable) {
    if (!variable.value) {
        return L"Unavailable";
    }
    const auto type = variable.value->type_flags & 0x7f;
    switch (type) {
        case 0:
            return L"0 (signed 8-bit)";
        case 1:
            return L"1 (integer)";
        case 2:
            return L"2 (boolean)";
        default:
            return std::to_wstring(type) + L" (raw)";
    }
}

std::wstring state_variable_text(const StateVariable& variable) {
    auto result = variable.name + L": ";
    if (variable.value) {
        result += std::to_wstring(variable.value->raw_value) + L" (" +
                  hex(static_cast<std::uint32_t>(variable.value->raw_value)) + L") / type " +
                  state_variable_type(variable) + L" / flags " + hex(variable.value->type_flags);
        if (variable.value->raw_value >= 32 && variable.value->raw_value <= 126) {
            result += L" / ASCII projection: '";
            result.push_back(static_cast<wchar_t>(variable.value->raw_value));
            result += L"'";
        }
    } else {
        result += L"value unavailable";
    }
    result += variable.key.state_database ? L" / State ID " : L" / HDB ID ";
    result += std::to_wstring(variable.key.id);
    if (!variable.registration.empty()) {
        result += L" / " + variable.registration;
    }
    return result;
}

void reset_state_baseline(StateVariableView& view, const GameSnapshot& snapshot) {
    view.baseline = snapshot.variables;
    view.baseline_index.clear();
    for (std::size_t index = 0; index < view.baseline.size(); ++index) {
        const auto& key = view.baseline[index].key;
        const auto [entry, inserted] = view.baseline_index.emplace(
            std::make_tuple(key.state_database, key.class_id, key.id), index);
        if (!inserted) {
            entry->second = view.baseline.size();
        }
    }
    view.manager = snapshot.manager;
    view.state = snapshot.state;
    view.hdb = snapshot.hdb;
    view.application = snapshot.application;
    view.session = snapshot.session;
}

const StateVariable* selected_state_variable(HWND list, const StateVariableView& view,
                                             const GameSnapshot& snapshot) {
    const auto selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (selected < 0 || static_cast<std::size_t>(selected) >= view.rows.size() ||
        view.rows[selected] >= snapshot.variables.size()) {
        return nullptr;
    }
    return &snapshot.variables[view.rows[selected]];
}

void update_state_variables(HWND list, StateVariableView& view, const GameSnapshot& snapshot,
                            std::wstring_view query, StateVariableFilter filter, bool nonzero,
                            bool changed, bool watched) {
    LVITEMW selected{};
    selected.mask = LVIF_PARAM;
    selected.iItem = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (selected.iItem >= 0 && ListView_GetItem(list, &selected)) {
        const auto index = static_cast<std::size_t>(selected.lParam);
        if (index < view.keys.size()) {
            view.selected = view.keys[index];
        }
    }
    const auto top = ListView_GetTopIndex(list);
    const auto top_key = top >= 0 && static_cast<std::size_t>(top) < view.keys.size()
                             ? std::optional(view.keys[top])
                             : std::nullopt;
    RECT top_rect{};
    ListView_GetItemRect(list, top, &top_rect, LVIR_BOUNDS);
    if (view.manager != snapshot.manager || view.state != snapshot.state ||
        view.hdb != snapshot.hdb || view.application != snapshot.application ||
        view.session != snapshot.session) {
        reset_state_baseline(view, snapshot);
        view.selected.reset();
        view.watches.clear();
        view.cells.clear();
    }
    const auto search = fold(std::wstring(query));
    view.current_counts.clear();
    for (const auto& row : snapshot.variables) {
        ++view.current_counts[{row.key.state_database, row.key.class_id, row.key.id}];
    }
    view.rows.clear();
    for (std::size_t index = 0; index < snapshot.variables.size(); ++index) {
        const auto& row = snapshot.variables[index];
        const auto* before = previous(view, row);
        if ((watched && !is_state_watched(view, row.key)) ||
            (filter == StateVariableFilter::where_are_we &&
             row.name.find(L"WhereAreWe") == std::wstring::npos) ||
            (filter == StateVariableFilter::registered && row.registration.empty()) ||
            (filter == StateVariableFilter::state && !row.key.state_database) ||
            (filter == StateVariableFilter::hdb && row.key.state_database) ||
            (nonzero && (!row.value || row.value->raw_value == 0)) ||
            (changed && (!before || !before->value || !row.value ||
                         before->value->raw_value == row.value->raw_value)) ||
            (!search.empty() &&
             fold(state_variable_text(row)).find(search) == std::wstring::npos)) {
            continue;
        }
        view.rows.push_back(index);
    }
    std::stable_sort(view.rows.begin(), view.rows.end(), [&](auto a, auto b) {
        const auto& left = snapshot.variables[a];
        const auto& right = snapshot.variables[b];
        int order = 0;
        if (view.sort_column == 1 && left.value && right.value) {
            order = (left.value->raw_value > right.value->raw_value) -
                    (left.value->raw_value < right.value->raw_value);
        } else if (view.sort_column == 2) {
            const auto* left_before = previous(view, left);
            const auto* right_before = previous(view, right);
            if (left_before && left_before->value && right_before && right_before->value) {
                order = (left_before->value->raw_value > right_before->value->raw_value) -
                        (left_before->value->raw_value < right_before->value->raw_value);
            } else {
                order = (left_before && left_before->value) - (right_before && right_before->value);
            }
        } else if (view.sort_column == 5) {
            order = (left.key.id > right.key.id) - (left.key.id < right.key.id);
        } else {
            order = column(left, view.sort_column, previous(view, left))
                        .compare(column(right, view.sort_column, previous(view, right)));
        }
        return view.descending ? order > 0 : order < 0;
    });
    std::vector<std::array<std::wstring, 7>> cells;
    std::vector<DatabaseObjectKey> keys;
    for (const auto index : view.rows) {
        const auto& row = snapshot.variables[index];
        const auto* before = previous(view, row);
        std::array<std::wstring, 7> fields;
        for (int field = 0; field < 7; ++field) {
            fields[field] = column(row, field, before);
        }
        cells.push_back(std::move(fields));
        keys.push_back(row.key);
    }
    if (cells == view.cells && keys == view.keys) {
        return;
    }
    view.cells = std::move(cells);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    view.keys.clear();
    for (std::size_t index = 0; index < view.rows.size(); ++index) {
        const auto& row = snapshot.variables[view.rows[index]];
        auto& text = view.cells[index][0];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(index);
        item.pszText = text.data();
        item.lParam = static_cast<LPARAM>(index);
        view.keys.push_back(row.key);
        const auto inserted = ListView_InsertItem(list, &item);
        for (int field = 1; field < 7; ++field) {
            ListView_SetItemText(list, inserted, field, view.cells[index][field].data());
        }
        if (view.selected && *view.selected == row.key) {
            ListView_SetItemState(list, inserted, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
    if (!view.rows.empty()) {
        auto restore = std::min(top, static_cast<int>(view.rows.size()) - 1);
        if (top_key) {
            const auto found = std::find(view.keys.begin(), view.keys.end(), *top_key);
            if (found != view.keys.end()) {
                restore = static_cast<int>(found - view.keys.begin());
            }
        }
        RECT first{};
        if (ListView_GetItemRect(list, 0, &first, LVIR_BOUNDS)) {
            ListView_Scroll(list, 0,
                            restore * (first.bottom - first.top) +
                                (top_rect.bottom > top_rect.top ? first.top - top_rect.top : 0));
        }
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, FALSE);
}

bool is_state_watched(const StateVariableView& view, const DatabaseObjectKey& key) {
    return std::find(view.watches.begin(), view.watches.end(), key) != view.watches.end();
}

void toggle_state_watch(StateVariableView& view, const DatabaseObjectKey& key) {
    const auto found = std::find(view.watches.begin(), view.watches.end(), key);
    if (found == view.watches.end()) {
        view.watches.push_back(key);
    } else {
        view.watches.erase(found);
    }
}

void copy_state_variable(HWND list, const StateVariableView& view, const GameSnapshot& snapshot) {
    const auto* row = selected_state_variable(list, view, snapshot);
    if (!row) {
        return;
    }
    const auto text = state_variable_text(*row);
    const auto bytes = (text.size() + 1) * sizeof(wchar_t);
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        return;
    }
    auto* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return;
    }
    std::memcpy(target, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (OpenClipboard(list)) {
        EmptyClipboard();
        const auto transferred = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        CloseClipboard();
        if (transferred) {
            return;
        }
    }
    GlobalFree(memory);
}
}
