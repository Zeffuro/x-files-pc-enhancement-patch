#include "devtools/game_state.h"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::wstring label(HWND tree, HTREEITEM node) {
    wchar_t value[2048]{};
    TVITEMW item{};
    item.mask = TVIF_TEXT;
    item.hItem = node;
    item.pszText = value;
    item.cchTextMax = 2048;
    TreeView_GetItem(tree, &item);
    return value;
}
}

int main() {
    HWND parent = nullptr;
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TREEVIEW_CLASSES};
        require(InitCommonControlsEx(&controls), "Cannot initialize tree controls");
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 800, 600, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        const auto tree =
            CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | TVS_HASBUTTONS, 0, 0, 780, 580, parent,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent && tree, "Cannot create state tree");
        devtools::GameSnapshot snapshot;
        snapshot.groups = {{L"Session", {L"Scene active: 1"}},
                           {L"Variables named WhereAreWe",
                            {L"cAIFieldOfficeWhereAreWe: 80 / State ID 60",
                             L"cAITarakanWhereAreWe: 0 / State ID 61"}},
                           {L"Other cached state variables", {L"bAIFieldOffice_Phone: 0"}}};
        devtools::update_state_tree(tree, snapshot);
        require(TreeView_GetCount(tree) == 7, "Unfiltered state lost groups or values");
        const auto session = TreeView_GetRoot(tree);
        const auto phases = TreeView_GetNextSibling(tree, session);
        TreeView_Expand(tree, phases, TVE_EXPAND);
        TreeView_SelectItem(tree, TreeView_GetChild(tree, phases));
        snapshot.groups[1].values[0] = L"cAIFieldOfficeWhereAreWe: 65 / State ID 60";
        devtools::update_state_tree(tree, snapshot);
        require(label(tree, TreeView_GetSelection(tree)).find(L": 65") != std::wstring::npos &&
                    (TreeView_GetItemState(tree, phases, TVIS_EXPANDED) & TVIS_EXPANDED),
                "Live refresh lost selected variable or expansion");
        devtools::update_state_tree(tree, snapshot, L"fieldoffice");
        require(TreeView_GetCount(tree) == 4 &&
                    label(tree, TreeView_GetRoot(tree)).find(L"WhereAreWe") != std::wstring::npos,
                "Case-insensitive name search lost matching values or included unrelated groups");
        devtools::update_state_tree(tree, snapshot, L"65");
        require(TreeView_GetCount(tree) == 2 &&
                    label(tree, TreeView_GetChild(tree, TreeView_GetRoot(tree))).find(L": 65") !=
                        std::wstring::npos,
                "Raw value search did not filter the held snapshot");
        devtools::update_state_tree(tree, snapshot, L"wherearewe");
        require(TreeView_GetCount(tree) == 3 &&
                    (TreeView_GetItemState(tree, TreeView_GetRoot(tree), TVIS_EXPANDED) &
                     TVIS_EXPANDED),
                "Group search did not expose the full matching group");
        devtools::update_state_tree(tree, snapshot, L"missing");
        require(TreeView_GetCount(tree) == 0 && !TreeView_GetSelection(tree),
                "No-match search retained stale state rows or selection");
        devtools::update_state_tree(tree, snapshot);
        require(TreeView_GetCount(tree) == 7 && snapshot.groups[1].values.size() == 2,
                "Clearing search lost snapshot coverage");
        const auto selected =
            TreeView_GetChild(tree, TreeView_GetNextSibling(tree, TreeView_GetRoot(tree)));
        TreeView_SelectItem(tree, selected);
        snapshot.groups[1].values.erase(snapshot.groups[1].values.begin());
        devtools::update_state_tree(tree, snapshot);
        require(!TreeView_GetSelection(tree),
                "Removed variable retained a different row selection");
        snapshot.groups[1].values = {L"<name uncached>: 0 / State ID 60",
                                     L"<name uncached>: 1 / State ID 61"};
        devtools::update_state_tree(tree, snapshot);
        const auto repeated = TreeView_GetNextSibling(tree, TreeView_GetRoot(tree));
        TreeView_SelectItem(tree, TreeView_GetChild(tree, repeated));
        devtools::update_state_tree(tree, snapshot);
        require(label(tree, TreeView_GetSelection(tree)).ends_with(L"State ID 60"),
                "Repeated variable names restored another record's selection");
        DestroyWindow(parent);
        std::cout << "Game State name/value search and refresh selection passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
