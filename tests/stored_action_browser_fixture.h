#pragma once
#include "stored_list_fixture.h"
#include "devtools/database/browser.h"
#include <commctrl.h>
#include <stdexcept>

namespace stored_action_browser_fixture {
inline void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

inline std::wstring text(HWND window) {
    std::wstring result(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    GetWindowTextW(window, result.data(), static_cast<int>(result.size()));
    result.resize(wcslen(result.c_str()));
    return result;
}

inline std::wstring properties(HWND browser) {
    const auto tree = GetDlgItem(browser, 3113);
    std::vector<HTREEITEM> pending{TreeView_GetRoot(tree)};
    std::wstring result;
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        if (!node) {
            continue;
        }
        wchar_t label[2048]{};
        TVITEMW item{};
        item.mask = TVIF_TEXT;
        item.hItem = node;
        item.pszText = label;
        item.cchTextMax = 2048;
        TreeView_GetItem(tree, &item);
        result += std::wstring(label) + L"\n";
        pending.push_back(TreeView_GetNextSibling(tree, node));
        pending.push_back(TreeView_GetChild(tree, node));
    }
    return result;
}

inline void choose(HWND browser, int control, unsigned value) {
    SendMessageW(GetDlgItem(browser, control), CB_SETCURSEL, value, 0);
    SendMessageW(browser, WM_COMMAND, MAKEWPARAM(control, CBN_SELCHANGE), 0);
}

inline void select(HWND list, int row) {
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

inline std::vector<std::uint8_t> fixture() {
    using namespace stored_list_fixture;
    auto bytes = make();
    word(bytes, 86, 0x53);
    word(bytes, 118, 0x53);
    word(bytes, 518, 900);
    word(bytes, 522, 7);
    const std::string name = "stored";
    std::copy(name.begin(), name.end(), bytes.begin() + 900);
    bytes[906] = 0;
    word(bytes, 646, 800);
    word(bytes, 650, 23);
    bytes[654] = 0x80;
    word(bytes, 800, 200);
    word(bytes, 804, 0xffffffff);
    bytes[808] = 0;
    bytes[809] = 2;
    bytes[810] = 1;
    bytes[811] = 0x37;
    word(bytes, 812, 200);
    word(bytes, 816, 0x12345678);
    bytes[820] = 0;
    bytes[821] = 2;
    bytes[822] = 0;
    return bytes;
}
}
