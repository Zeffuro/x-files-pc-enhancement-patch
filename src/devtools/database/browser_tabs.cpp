#include "browser_state.h"
#include <algorithm>

namespace devtools::database_browser {
Pane selected_pane(const Browser& state) {
    TCITEMW item{};
    item.mask = TCIF_PARAM;
    const int index = TabCtrl_GetCurSel(state.tabs);
    return index >= 0 && TabCtrl_GetItem(state.tabs, index, &item) ? static_cast<Pane>(item.lParam)
                                                                   : Pane::overview;
}

void select_pane(Browser& state, Pane pane) {
    for (int index = 0; index < TabCtrl_GetItemCount(state.tabs); ++index) {
        TCITEMW item{};
        item.mask = TCIF_PARAM;
        if (TabCtrl_GetItem(state.tabs, index, &item) && item.lParam == static_cast<LPARAM>(pane)) {
            TabCtrl_SetCurSel(state.tabs, index);
            if (const auto* row = selected_row(state)) {
                if (state.mode_index == 7) {
                    state.database_pane = pane;
                } else if (native_mode(state) && row->object) {
                    state.native_pane = pane;
                } else if (state.mode_index == 6 && row->asset) {
                    state.asset_panes[state.assets.assets[*row->asset].type] = pane;
                }
            }
            return;
        }
    }
    TabCtrl_SetCurSel(state.tabs, 0);
}

void configure_tabs(Browser& state) {
    const auto previous = selected_pane(state);
    const auto* row = selected_row(state);
    const auto* asset = row && row->asset ? &state.assets.assets[*row->asset] : nullptr;
    std::vector<std::pair<Pane, const wchar_t*>> panes{{Pane::overview, L"Overview"}};
    if (row && (row->stored || row->object)) {
        panes.emplace_back(Pane::fields, L"Fields");
    }
    if (!state.links.empty()) {
        panes.emplace_back(Pane::links, L"Links");
    }
    if (row || (state.database && state.mode_index != 6)) {
        panes.emplace_back(Pane::raw, L"Raw bytes");
    }
    if ((asset && asset->type == L"Text") || (row && state.mode_index == 2)) {
        panes.emplace_back(Pane::text, L"Text");
    }
    if (asset && asset->type == L"Font") {
        panes.emplace_back(Pane::font, L"Font");
    }
    if (asset && asset->type == L"Localization") {
        panes.emplace_back(Pane::strings, L"Strings");
    }
    bool changed = TabCtrl_GetItemCount(state.tabs) != static_cast<int>(panes.size());
    for (int index = 0; !changed && index < static_cast<int>(panes.size()); ++index) {
        TCITEMW item{};
        item.mask = TCIF_PARAM;
        changed = !TabCtrl_GetItem(state.tabs, index, &item) ||
                  item.lParam != static_cast<LPARAM>(panes[static_cast<std::size_t>(index)].first);
    }
    if (changed) {
        TabCtrl_DeleteAllItems(state.tabs);
        int index = 0;
        for (const auto& [pane, name] : panes) {
            TCITEMW item{};
            item.mask = TCIF_TEXT | TCIF_PARAM;
            item.pszText = const_cast<LPWSTR>(name);
            item.lParam = static_cast<LPARAM>(pane);
            TabCtrl_InsertItem(state.tabs, index++, &item);
        }
    }
    auto preferred = state.mode_index == 7 && row ? state.database_pane : previous;
    if (native_mode(state) && row && row->object) {
        preferred = state.native_pane;
    }
    if (state.mode_index == 6 && asset) {
        const auto saved = state.asset_panes.find(asset->type);
        preferred = saved != state.asset_panes.end() ? saved->second : Pane::overview;
    }
    select_pane(state, preferred);
    layout(state);
}
}
