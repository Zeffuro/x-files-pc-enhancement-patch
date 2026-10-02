#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace devtools::inspector {
struct PreviewDetail {
    std::wstring key, parent, text;
    bool expanded = false;
};

class PreviewDetailsTree {
public:
    void update(HWND window, const std::vector<PreviewDetail>& desired) {
        const bool unchanged = desired.size() == items.size() &&
                               std::all_of(desired.begin(), desired.end(), [&](const auto& row) {
                                   const auto found = items.find(row.key);
                                   return found != items.end() && found->second.text == row.text;
                               });
        if (unchanged) {
            return;
        }
        std::set<std::wstring> retained;
        for (const auto& row : desired) {
            retained.insert(row.key);
        }
        SendMessageW(window, WM_SETREDRAW, FALSE, 0);
        for (auto old = order.rbegin(); old != order.rend(); ++old) {
            if (!retained.contains(*old)) {
                TreeView_DeleteItem(window, items.at(*old).item);
                items.erase(*old);
            }
        }
        order.clear();
        std::vector<HTREEITEM> expanded;
        for (const auto& row : desired) {
            auto found = items.find(row.key);
            if (found == items.end()) {
                TVINSERTSTRUCTW insert{};
                insert.hParent = row.parent.empty() ? TVI_ROOT : items.at(row.parent).item;
                insert.hInsertAfter = TVI_LAST;
                insert.item.mask = TVIF_TEXT;
                insert.item.pszText = const_cast<LPWSTR>(row.text.c_str());
                const auto item = TreeView_InsertItem(window, &insert);
                found = items.emplace(row.key, Item{item, row.text}).first;
                if (row.expanded) {
                    expanded.push_back(item);
                }
            } else if (found->second.text != row.text) {
                TVITEMW item{};
                item.mask = TVIF_TEXT;
                item.hItem = found->second.item;
                item.pszText = const_cast<LPWSTR>(row.text.c_str());
                TreeView_SetItem(window, &item);
                found->second.text = row.text;
            }
            order.push_back(row.key);
        }
        for (const auto item : expanded) {
            TreeView_Expand(window, item, TVE_EXPAND);
        }
        SendMessageW(window, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(window, nullptr, FALSE);
    }

private:
    struct Item {
        HTREEITEM item;
        std::wstring text;
    };

    std::map<std::wstring, Item> items;
    std::vector<std::wstring> order;
};
}
