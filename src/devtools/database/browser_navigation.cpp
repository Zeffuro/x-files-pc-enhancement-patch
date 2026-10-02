#include "browser_state.h"
#include <algorithm>

namespace devtools::database_browser {
BrowserView current_view(const Browser& state) {
    BrowserView view;
    view.mode = state.mode_index;
    view.filter = state.filter_index;
    view.class_id = filter_class(state);
    view.query = text(state.search);
    view.offset = state.manual_offset;
    view.pane = selected_pane(state);
    view.database_pane = state.database_pane;
    view.sort_column = state.sort_column;
    view.sort_descending = state.sort_descending;
    view.top = ListView_GetTopIndex(state.list);
    view.valid = true;
    if (const auto* row = selected_row(state)) {
        view.selection = row->key;
        if (view.mode == 6 && row->asset) {
            const auto& asset = state.assets.assets[*row->asset];
            view.asset = asset.path;
            view.asset_type = asset.type;
            const auto saved = state.asset_panes.find(asset.type);
            view.asset_pane = saved != state.asset_panes.end() ? saved->second : Pane::overview;
        }
    }
    return view;
}

void restore_view(Browser& state, const BrowserView& view) {
    state.mode_index = view.mode;
    if (view.mode == 7) {
        state.database_pane = view.database_pane;
    } else if (view.mode == 6 && !view.asset_type.empty()) {
        state.asset_panes[view.asset_type] = view.asset_pane;
    }
    state.rows.clear();
    state.pending_selection.reset();
    state.manual_offset = view.offset;
    state.sort_column = view.sort_column;
    state.sort_descending = view.sort_descending;
    select_mode(state);
    configure_filter(state);
    if (view.mode == 6 &&
        static_cast<LRESULT>(view.filter) < SendMessageW(state.filter, CB_GETCOUNT, 0, 0)) {
        state.filter_index = view.filter;
    } else if (view.class_id) {
        for (LRESULT index = 0; index < SendMessageW(state.filter, CB_GETCOUNT, 0, 0); ++index) {
            if (SendMessageW(state.filter, CB_GETITEMDATA, index, 0) ==
                static_cast<LRESULT>(view.class_id)) {
                state.filter_index = static_cast<unsigned>(index);
                break;
            }
        }
    }
    SendMessageW(state.filter, CB_SETCURSEL, state.filter_index, 0);
    state.rebuilding = true;
    SetWindowTextW(state.search, view.query.c_str());
    ListView_SetItemState(state.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    state.rebuilding = false;
    state.pending_selection = view.selection;
    if (view.asset) {
        state.pending_selection = database_asset_find(state.assets, *view.asset);
    }
    populate(state);
    select_pane(state, view.pane);
    if (view.mode == 7) {
        state.database_pane = view.database_pane;
    } else if (view.mode == 6 && !view.asset_type.empty()) {
        state.asset_panes[view.asset_type] = view.asset_pane;
    }
    if (view.top >= 0 && view.top < static_cast<int>(state.rows.size())) {
        ListView_EnsureVisible(state.list, static_cast<int>(state.rows.size()) - 1, FALSE);
        ListView_EnsureVisible(state.list, view.top, FALSE);
    }
    layout(state);
}

void switch_view(Browser& state, unsigned mode) {
    if (mode == state.mode_index || mode >= state.views.size()) {
        return;
    }
    state.views[state.mode_index] = current_view(state);
    auto next = state.views[mode];
    if (!next.valid) {
        next.mode = mode;
    }
    restore_view(state, next);
    SetFocus(state.search);
}

void update_navigation(Browser& state) {
    const bool assets = state.mode_index == 6, stored = state.mode_index == 7;
    SendMessageW(state.asset_browse, BM_SETCHECK, assets ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(state.database_view, BM_SETCHECK, stored ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(state.database_heading, assets ? L"Tables (switch view)" : L"Database tables");
    std::wstring context;
    if (assets) {
        context = L"Asset files | " + state.root.wstring();
    } else if (stored) {
        const auto cls = filter_class(state);
        context = L"Database tables | " + state.path.filename().wstring() + L" | " +
                  (cls ? std::wstring(database_class_name(cls)) : L"All records");
    } else {
        wchar_t mode[128]{};
        GetWindowTextW(state.mode, mode, 128);
        context = std::wstring(mode) + L" | " + state.path.filename().wstring();
    }
    SetWindowTextW(state.list_context, context.c_str());
    SendMessageW(state.search, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(assets ? L"Search asset filename, path or label (Ctrl+F)"
                                                 : L"Search record ID, name or field (Ctrl+F)"));
}

void navigate(Browser& state, const Destination& destination, bool remember) {
    const auto stored =
        destination.stored && destination.object
            ? game_assets::find_database_record(state.stored, destination.object->class_id,
                                                destination.object->id)
            : std::optional<std::size_t>{};
    const auto object = !destination.stored && destination.object
                            ? database_find(state.native, *destination.object)
                            : std::optional<std::size_t>{};
    const auto asset = !destination.asset.empty()
                           ? database_asset_find(state.assets, destination.asset)
                           : std::optional<std::size_t>{};
    if (!object && !asset && !stored) {
        describe(state);
        return;
    }
    const auto source = current_view(state);
    state.views[state.mode_index] = source;
    if (remember && source.selection) {
        if (state.history.size() == 64) {
            state.history.erase(state.history.begin());
        }
        state.history.push_back(source);
    }
    state.mode_index = stored ? 7u : object ? 0u : 6u;
    state.filter_index = 0;
    state.rows.clear();
    state.manual_offset.reset();
    select_mode(state);
    configure_filter(state);
    state.rebuilding = true;
    SetWindowTextW(state.search, L"");
    state.rebuilding = false;
    state.pending_selection = object || stored ? row_key(*destination.object) : *asset;
    populate(state);
    SetFocus(state.list);
}

void follow(Browser& state) {
    const int choice = ListView_GetNextItem(state.relations, -1, LVNI_SELECTED);
    if (choice >= 0 && std::size_t(choice) < state.links.size()) {
        navigate(state, state.links[static_cast<std::size_t>(choice)].destination, true);
    }
}

}
