#include "game_state.h"
#include <commctrl.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <map>

namespace devtools {
namespace {
std::wstring item_text(HWND tree, HTREEITEM item) {
    wchar_t text[2048]{};
    TVITEMW value{};
    value.mask = TVIF_TEXT;
    value.hItem = item;
    value.pszText = text;
    value.cchTextMax = static_cast<int>(std::size(text));
    TreeView_GetItem(tree, &value);
    return text;
}

HTREEITEM update(HWND tree, HTREEITEM parent, HTREEITEM item, const std::wstring& text) {
    if (!item) {
        TVINSERTSTRUCTW value{};
        value.hParent = parent;
        value.hInsertAfter = TVI_LAST;
        value.item.mask = TVIF_TEXT;
        value.item.pszText = const_cast<wchar_t*>(text.c_str());
        return TreeView_InsertItem(tree, &value);
    }
    if (item_text(tree, item) != text) {
        TVITEMW value{};
        value.mask = TVIF_TEXT;
        value.hItem = item;
        value.pszText = const_cast<wchar_t*>(text.c_str());
        TreeView_SetItem(tree, &value);
    }
    return item;
}

void remove_rest(HWND tree, HTREEITEM item) {
    while (item) {
        const auto next = TreeView_GetNextSibling(tree, item);
        TreeView_DeleteItem(tree, item);
        item = next;
    }
}
}

void update_state_tree(HWND tree, const GameSnapshot& snapshot, std::wstring_view query) {
    const auto folded = [](std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        return value;
    };
    const auto search = folded(std::wstring(query));
    const auto selected = TreeView_GetSelection(tree);
    const auto parent = selected ? TreeView_GetParent(tree, selected) : nullptr;
    const auto name = [](std::wstring value) {
        const auto identity = value.rfind(L" / ");
        if (identity != std::wstring::npos &&
            (value.substr(identity).starts_with(L" / State ID ") ||
             value.substr(identity).starts_with(L" / HDB ID "))) {
            return value.substr(identity);
        }
        const auto separator = value.find(L": ");
        return separator == std::wstring::npos ? value : value.substr(0, separator);
    };
    const auto group_name = [](std::wstring value) {
        const auto count = value.rfind(L" (");
        return count == std::wstring::npos ? value : value.substr(0, count);
    };
    const auto selected_group = group_name(item_text(tree, parent ? parent : selected));
    const auto selected_name = parent ? name(item_text(tree, selected)) : L"";
    std::map<std::wstring, bool> expanded;
    for (auto group = TreeView_GetRoot(tree); group; group = TreeView_GetNextSibling(tree, group)) {
        expanded[group_name(item_text(tree, group))] =
            (TreeView_GetItemState(tree, group, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
    }
    SendMessageW(tree, WM_SETREDRAW, FALSE, 0);
    TreeView_SelectItem(tree, nullptr);
    auto branch = TreeView_GetRoot(tree);
    for (const auto& group : snapshot.groups) {
        std::vector<const std::wstring*> values;
        const bool matches =
            search.empty() || folded(group.name).find(search) != std::wstring::npos;
        for (const auto& value : group.values) {
            if (matches || folded(value).find(search) != std::wstring::npos) {
                values.push_back(&value);
            }
        }
        if (!matches && values.empty()) {
            continue;
        }
        const bool added = !branch;
        branch = update(tree, TVI_ROOT, branch,
                        group.name + L" (" + std::to_wstring(values.size()) + L")");
        auto child = TreeView_GetChild(tree, branch);
        for (const auto* value : values) {
            child = update(tree, branch, child, *value);
            if (group.name == selected_group && name(*value) == selected_name) {
                TreeView_SelectItem(tree, child);
            }
            child = TreeView_GetNextSibling(tree, child);
        }
        remove_rest(tree, child);
        const auto previous = expanded.find(group.name);
        const bool open = !search.empty() || (previous != expanded.end() && previous->second) ||
                          (added && group.name == L"Session");
        TreeView_Expand(tree, branch, open ? TVE_EXPAND : TVE_COLLAPSE);
        if (group.name == selected_group && selected_name.empty()) {
            TreeView_SelectItem(tree, branch);
        }
        branch = TreeView_GetNextSibling(tree, branch);
    }
    remove_rest(tree, branch);
    SendMessageW(tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(tree, nullptr, FALSE);
}

void copy_state_item(HWND tree) {
    const auto selected = TreeView_GetSelection(tree);
    if (!selected) {
        return;
    }
    const auto text = item_text(tree, selected);
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
    if (OpenClipboard(tree)) {
        EmptyClipboard();
        const bool transferred = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        CloseClipboard();
        if (transferred) {
            return;
        }
    }
    GlobalFree(memory);
}
}
