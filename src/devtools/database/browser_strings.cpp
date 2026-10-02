#include "browser_state.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace devtools::database_browser {
namespace {
std::wstring visible_text(const std::wstring& value, bool single_line) {
    std::wstring result;
    for (std::size_t at = 0; at < value.size(); ++at) {
        const auto ch = value[at];
        if (ch == L'\0') {
            result += L"\\0";
        } else if (!single_line && ch == L'\r') {
            if (at + 1 < value.size() && value[at + 1] == L'\n') {
                ++at;
            }
            result += L"\r\n";
        } else if (!single_line && ch == L'\n') {
            result += L"\r\n";
        } else if (single_line && ch == L'\r') {
            result += L"\\r";
        } else if (single_line && ch == L'\n') {
            result += L"\\n";
        } else if (ch < L' ' && ch != L'\r' && ch != L'\n' && ch != L'\t') {
            result += L"\\u" + hex(static_cast<unsigned>(ch)).substr(6);
        } else {
            result += ch;
        }
    }
    return result;
}
}

void create_string_controls(Browser& state, HMODULE module, HFONT font) {
    const auto child = [&](const wchar_t* kind, DWORD style, int id) {
        const auto control =
            CreateWindowExW(0, kind, L"", WS_CHILD | WS_TABSTOP | style, 0, 0, 0, 0, state.window,
                            reinterpret_cast<HMENU>(INT_PTR(id)), module, nullptr);
        if (!control) {
            throw std::runtime_error("Cannot create localization string controls");
        }
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SetWindowSubclass(control, control_keys, 1, reinterpret_cast<DWORD_PTR>(state.window));
        return control;
    };
    state.strings_search = child(L"EDIT", ES_AUTOHSCROLL | WS_BORDER, strings_search_id);
    SendMessageW(state.strings_search, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Filter strings, resource ID or language ID"));
    SendMessageW(state.strings_search, EM_SETLIMITTEXT, 512, 0);
    state.strings_copy = child(L"BUTTON", BS_PUSHBUTTON, strings_copy_id);
    SetWindowTextW(state.strings_copy, L"Copy full string");
    state.strings_list = child(
        WC_LISTVIEWW, LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER,
        strings_list_id);
    ListView_SetExtendedListViewStyle(state.strings_list,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    int index = 0;
    for (const auto* name : {L"ID", L"Language", L"File offset", L"Text"}) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = const_cast<LPWSTR>(name);
        ListView_InsertColumn(state.strings_list, index++, &column);
    }
    state.strings_text = child(L"EDIT",
                               ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                                   WS_VSCROLL | WS_HSCROLL | WS_BORDER,
                               strings_text_id);
    SendMessageW(state.strings_text, EM_SETLIMITTEXT, 2 * 1024 * 1024, 0);
}

void layout_strings(Browser& state, int x, int y, int width, int height, bool visible) {
    const int list_height = std::max(30, (height - 38) * 55 / 100);
    MoveWindow(state.strings_search, x, y, std::max(40, width - 135), 26, TRUE);
    MoveWindow(state.strings_copy, x + std::max(45, width - 130), y, 130, 26, TRUE);
    MoveWindow(state.strings_list, x, y + 34, width, list_height, TRUE);
    MoveWindow(state.strings_text, x, y + 38 + list_height, width,
               std::max(10, height - 38 - list_height), TRUE);
    ListView_SetColumnWidth(state.strings_list, 0, 65);
    ListView_SetColumnWidth(state.strings_list, 1, 105);
    ListView_SetColumnWidth(state.strings_list, 2, 110);
    ListView_SetColumnWidth(state.strings_list, 3, std::max(140, width - 300));
    for (const auto control :
         {state.strings_search, state.strings_list, state.strings_text, state.strings_copy}) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    }
}

void select_string(Browser& state) {
    const int selected = ListView_GetNextItem(state.strings_list, -1, LVNI_SELECTED);
    EnableWindow(state.strings_copy,
                 selected >= 0 && std::size_t(selected) < state.string_indices.size());
    std::wstring content = state.resource_strings.status;
    if (selected >= 0 && std::size_t(selected) < state.string_indices.size()) {
        const auto& entry = state.resource_strings
                                .strings[state.string_indices[static_cast<std::size_t>(selected)]];
        auto preview = entry.text.substr(0, 8192);
        if (!preview.empty() && preview.back() >= 0xd800 && preview.back() <= 0xdbff) {
            preview.pop_back();
        }
        content = L"Resource ID: " + std::to_wstring(entry.id) + L"\r\nLanguage ID: " +
                  std::to_wstring(entry.language) + L" (" + hex(entry.language) +
                  L")\r\nFile offset: " + hex(entry.file_offset) +
                  L"\r\nResource code page (raw): " + std::to_wstring(entry.code_page) +
                  L"\r\n\r\n" + visible_text(preview, false);
        if (preview.size() < entry.text.size()) {
            content += L"\r\n\r\nLong string preview shortened. Use Copy full string for the "
                       L"complete text.";
        }
    } else if (state.resource_strings.valid) {
        content += L"\r\nMatching strings: " + std::to_wstring(state.string_indices.size());
    }
    SetWindowTextW(state.strings_text, content.c_str());
}

void copy_string(Browser& state) {
    const int selected = ListView_GetNextItem(state.strings_list, -1, LVNI_SELECTED);
    if (selected < 0 || std::size_t(selected) >= state.string_indices.size()) {
        return;
    }
    const auto& entry = state.resource_strings.strings[state.string_indices[std::size_t(selected)]];
    const auto value = visible_text(entry.text, false);
    const auto bytes = (value.size() + 1) * sizeof(wchar_t);
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        throw std::runtime_error("Cannot allocate string clipboard data");
    }
    const auto target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        throw std::runtime_error("Cannot prepare string clipboard data");
    }
    std::memcpy(target, value.c_str(), bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(state.window)) {
        GlobalFree(memory);
        throw std::runtime_error("Cannot open the clipboard");
    }
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (!copied) {
        GlobalFree(memory);
        throw std::runtime_error("Cannot copy the string");
    }
}

void filter_strings(Browser& state) {
    const int selected = ListView_GetNextItem(state.strings_list, -1, LVNI_SELECTED);
    const auto old = selected >= 0 && std::size_t(selected) < state.string_indices.size()
                         ? std::optional<std::size_t>(state.string_indices[std::size_t(selected)])
                         : std::nullopt;
    state.rebuilding_strings = true;
    ListView_SetItemCount(state.strings_list, 0);
    state.string_rows.clear();
    state.string_indices.clear();
    const auto query = lower(text(state.strings_search));
    int choice = 0;
    for (std::size_t at = 0; at < state.resource_strings.strings.size(); ++at) {
        const auto& entry = state.resource_strings.strings[at];
        std::array<std::wstring, 4> row{std::to_wstring(entry.id),
                                        std::to_wstring(entry.language) + L" / " +
                                            hex(entry.language),
                                        hex(entry.file_offset), visible_text(entry.text, true)};
        if (lower(row[0] + L" " + row[1] + L" " + row[2] + L" " + row[3]).find(query) ==
            std::wstring::npos) {
            continue;
        }
        if (old == at) {
            choice = static_cast<int>(state.string_indices.size());
        }
        state.string_indices.push_back(at);
        state.string_rows.push_back(std::move(row));
    }
    ListView_SetItemCount(state.strings_list, static_cast<int>(state.string_indices.size()));
    ListView_SetItemState(state.strings_list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (!state.string_indices.empty()) {
        ListView_SetItemState(state.strings_list, choice, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(state.strings_list, choice, FALSE);
    }
    state.rebuilding_strings = false;
    select_string(state);
    InvalidateRect(state.strings_list, nullptr, FALSE);
}

void load_strings(Browser& state, const std::filesystem::path& path) {
    if (state.resource_strings_path == path) {
        return;
    }
    state.resource_strings_path = path;
    state.resource_strings = game_assets::load_resource_strings(path);
    state.rebuilding_strings = true;
    ListView_SetItemCount(state.strings_list, 0);
    state.string_indices.clear();
    state.string_rows.clear();
    SetWindowTextW(state.strings_search, L"");
    state.rebuilding_strings = false;
    filter_strings(state);
}

bool string_notification(Browser& state, NMHDR* notification) {
    if (notification->hwndFrom != state.strings_list) {
        return false;
    }
    if (notification->code == LVN_GETDISPINFOW) {
        auto& item = reinterpret_cast<NMLVDISPINFOW*>(notification)->item;
        if ((item.mask & LVIF_TEXT) && item.iItem >= 0 && item.iSubItem >= 0 &&
            std::size_t(item.iItem) < state.string_rows.size() && item.iSubItem < 4) {
            const auto& value =
                state.string_rows[std::size_t(item.iItem)][std::size_t(item.iSubItem)];
            wcsncpy_s(item.pszText, static_cast<std::size_t>(item.cchTextMax), value.c_str(),
                      _TRUNCATE);
        }
    } else if (notification->code == LVN_ITEMCHANGED && !state.rebuilding_strings) {
        select_string(state);
    }
    return true;
}
}
