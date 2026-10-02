#include "browser_state.h"
#include "field_guide.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace devtools::database_browser {
namespace {
std::wstring node_text(HWND tree, HTREEITEM node) {
    std::wstring result(16384, L'\0');
    TVITEMW item{};
    item.mask = TVIF_TEXT;
    item.hItem = node;
    item.pszText = result.data();
    item.cchTextMax = static_cast<int>(result.size());
    TreeView_GetItem(tree, &item);
    result.resize(wcslen(result.c_str()));
    return result;
}

void fields(Browser& state, HTREEITEM node, const std::wstring& group, std::uint32_t cls) {
    for (; node; node = TreeView_GetNextSibling(state.properties, node)) {
        const auto label = node_text(state.properties, node);
        const auto child = TreeView_GetChild(state.properties, node);
        if (child) {
            fields(state, child, group.empty() ? label : group + L" / " + label, cls);
        } else {
            const auto separator = label.find(L": ");
            const auto field =
                separator == std::wstring::npos ? L"Note" : label.substr(0, separator);
            const auto value =
                separator == std::wstring::npos ? label : label.substr(separator + 2);
            state.field_rows.push_back({group, field, value,
                                        separator == std::wstring::npos
                                            ? L"Context about this record or the displayed data."
                                            : database_field_meaning(cls, group, field)});
        }
    }
}

void select_field(Browser& state) {
    const int choice = ListView_GetNextItem(state.fields_list, -1, LVNI_SELECTED);
    if (choice < 0 || std::size_t(choice) >= state.field_rows.size()) {
        SetWindowTextW(state.fields_text,
                       L"Select a field to read its full value and explanation.");
        return;
    }
    const auto& row = state.field_rows[static_cast<std::size_t>(choice)];
    const auto content = row[0] + L" / " + row[1] + L"\r\nValue: " + row[2] + L"\r\n" + row[3];
    SetWindowTextW(state.fields_text, content.c_str());
}

std::wstring cell(std::wstring value) {
    for (auto& ch : value) {
        if (ch == L'\t' || ch == L'\r' || ch == L'\n') {
            ch = L' ';
        }
    }
    return value;
}
}

void create_table_controls(Browser& state, HMODULE module, HFONT font) {
    const auto child = [&](const wchar_t* type, const wchar_t* caption, DWORD style, int id) {
        const auto window = CreateWindowExW(
            0, type, caption, WS_CHILD | WS_CLIPSIBLINGS | style, 0, 0, 1, 1, state.window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module, nullptr);
        if (!window) {
            throw std::runtime_error("Cannot create database table control");
        }
        SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SetWindowSubclass(window, control_keys, 1,
                          reinterpret_cast<DWORD_PTR>(GetParent(state.window)));
        return window;
    };
    constexpr DWORD report =
        LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP;
    state.fields_list = child(WC_LISTVIEWW, L"", report, fields_list_id);
    state.fields_text =
        child(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
              fields_text_id);
    state.fields_copy = child(L"BUTTON", L"Copy table", BS_PUSHBUTTON | WS_TABSTOP, fields_copy_id);
    state.database_info =
        child(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
              database_info_id);
    state.database_classes = child(WC_LISTVIEWW, L"", report, database_classes_id);
    state.database_browse =
        child(L"BUTTON", L"Database overview", BS_PUSHBUTTON | WS_TABSTOP, database_browse_id);
    for (const auto list : {state.fields_list, state.database_classes}) {
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const wchar_t* field_headings[] = {L"Group", L"Field", L"Value", L"Meaning"};
        const wchar_t* class_headings[] = {L"Class", L"Stored type", L"Count", L"Used for"};
        for (int column = 0; column < 4; ++column) {
            LVCOLUMNW heading{};
            heading.mask = LVCF_TEXT;
            heading.pszText = const_cast<LPWSTR>(
                list == state.fields_list ? field_headings[column] : class_headings[column]);
            ListView_InsertColumn(list, column, &heading);
        }
    }
}

void layout_tables(Browser& state, int x, int y, int width, int height, Pane pane) {
    const int description = std::min(140, std::max(72, height / 3));
    MoveWindow(state.fields_list, x, y, width, std::max(10, height - description - 40), TRUE);
    MoveWindow(state.fields_text, x, y + height - description - 34, width, description, TRUE);
    MoveWindow(state.fields_copy, x, y + height - 28, 110, 28, TRUE);
    for (const auto control : {state.fields_list, state.fields_text, state.fields_copy}) {
        ShowWindow(control, pane == Pane::fields ? SW_SHOW : SW_HIDE);
    }
    ListView_SetColumnWidth(state.fields_list, 0, 110);
    ListView_SetColumnWidth(state.fields_list, 1, 150);
    ListView_SetColumnWidth(state.fields_list, 2, 110);
    ListView_SetColumnWidth(state.fields_list, 3, std::max(190, width - 390));
}

void update_field_table(Browser& state) {
    const auto* row = selected_row(state);
    const auto cls = row && row->stored   ? state.stored.records[*row->stored].class_id
                     : row && row->object ? state.native.objects[*row->object].class_id
                                          : 0;
    state.field_rows.clear();
    if (cls) {
        state.field_rows.push_back(
            {L"Guide", L"Used for", database_class_purpose(cls),
             L"Class role. Field meanings are explained individually below."});
        fields(state, TreeView_GetRoot(state.properties), L"", cls);
    }
    ListView_SetItemCount(state.fields_list, static_cast<int>(state.field_rows.size()));
    ListView_SetItemState(state.fields_list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (!state.field_rows.empty()) {
        ListView_SetItemState(state.fields_list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
    select_field(state);
    EnableWindow(state.fields_copy, !state.field_rows.empty());
    InvalidateRect(state.fields_list, nullptr, FALSE);
}

bool table_notification(Browser& state, NMHDR* notification) {
    const bool field = notification->hwndFrom == state.fields_list;
    if (!field && notification->hwndFrom != state.database_classes) {
        return false;
    }
    if (notification->code == LVN_GETDISPINFOW) {
        auto& item = reinterpret_cast<NMLVDISPINFOW*>(notification)->item;
        const auto& rows = field ? state.field_rows : state.class_rows;
        if ((item.mask & LVIF_TEXT) && item.iItem >= 0 && std::size_t(item.iItem) < rows.size() &&
            item.iSubItem >= 0 && item.iSubItem < 4) {
            wcsncpy_s(item.pszText, static_cast<std::size_t>(item.cchTextMax),
                      rows[static_cast<std::size_t>(item.iItem)][item.iSubItem].c_str(), _TRUNCATE);
        }
    } else if (notification->code == LVN_ITEMCHANGED) {
        if (field) {
            select_field(state);
        } else {
            const int choice = ListView_GetNextItem(state.database_classes, -1, LVNI_SELECTED);
            if (choice >= 0 && std::size_t(choice) < state.class_rows.size()) {
                const auto& row = state.class_rows[static_cast<std::size_t>(choice)];
                const auto info = L"Database file: " + state.path.wstring() + L"\r\n" + row[1] +
                                  L" (" + row[0] + L"): " + row[2] + L" records\r\n" + row[3] +
                                  L"\r\nSelect a record to inspect its fields.";
                SetWindowTextW(state.database_info, info.c_str());
                const auto* change = reinterpret_cast<NMLISTVIEW*>(notification);
                if (!state.rebuilding && (change->uChanged & LVIF_STATE) &&
                    (change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED)) {
                    browse_database_class(state);
                }
            }
        }
    } else if (!field && (notification->code == NM_DBLCLK ||
                          (notification->code == LVN_KEYDOWN &&
                           reinterpret_cast<NMLVKEYDOWN*>(notification)->wVKey == VK_RETURN))) {
        browse_database_class(state);
    } else if (field && notification->code == LVN_KEYDOWN) {
        const auto key = reinterpret_cast<NMLVKEYDOWN*>(notification)->wVKey;
        if (key == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            copy_field_table(state);
        }
    }
    return true;
}

void copy_field_table(Browser& state) {
    std::wstring content = L"Group\tField\tValue\tMeaning\r\n";
    for (const auto& row : state.field_rows) {
        for (std::size_t column = 0; column < row.size(); ++column) {
            if (column) {
                content += L'\t';
            }
            content += cell(row[column]);
        }
        content += L"\r\n";
    }
    if (!OpenClipboard(state.window)) {
        return;
    }
    const auto bytes = (content.size() + 1) * sizeof(wchar_t);
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        if (auto* target = GlobalLock(memory)) {
            std::memcpy(target, content.c_str(), bytes);
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
