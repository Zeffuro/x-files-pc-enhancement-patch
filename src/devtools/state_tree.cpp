#include "game_state.h"
#include <commctrl.h>
#include <cstring>

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

void update_state_tree(HWND tree, const GameSnapshot& snapshot) {
    auto branch = TreeView_GetRoot(tree);
    for (const auto& group : snapshot.groups) {
        const bool added = !branch;
        branch = update(tree, TVI_ROOT, branch,
                        group.name + L" (" + std::to_wstring(group.values.size()) + L")");
        auto child = TreeView_GetChild(tree, branch);
        for (const auto& value : group.values) {
            child = update(tree, branch, child, value);
            child = TreeView_GetNextSibling(tree, child);
        }
        remove_rest(tree, child);
        if (added && group.name == L"Session") {
            TreeView_Expand(tree, branch, TVE_EXPAND);
        }
        branch = TreeView_GetNextSibling(tree, branch);
    }
    remove_rest(tree, branch);
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
