#include "browser_state.h"
#include "pff_browser.h"
#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace devtools::database_browser {
constexpr wchar_t browser_class[] = L"XFilesDatabaseBrowser";

std::wstring text(HWND window) {
    std::wstring value(GetWindowTextLengthW(window) + 1, L'\0');
    value.resize(GetWindowTextW(window, value.data(), static_cast<int>(value.size())));
    return value;
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return wchar_t(std::towlower(ch)); });
    return value;
}

std::wstring hex(std::uint64_t value) {
    std::wostringstream out;
    out << L"0x" << std::hex << std::setw(8) << std::setfill(L'0') << value;
    return out.str();
}

std::uint64_t row_key(DatabaseObjectKey key) {
    return (std::uint64_t(key.class_id) << 33) | (std::uint64_t(key.id) << 1) |
           std::uint64_t(key.state_database);
}

bool native_mode(const Browser& state) {
    return state.mode_index == 0 || state.mode_index == 4 || state.mode_index == 5;
}

void select_mode(Browser& state) {
    const auto count = SendMessageW(state.mode, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index) {
        if (SendMessageW(state.mode, CB_GETITEMDATA, index, 0) ==
            static_cast<LRESULT>(state.mode_index)) {
            SendMessageW(state.mode, CB_SETCURSEL, index, 0);
            return;
        }
    }
}

const Row* selected_row(const Browser& state) {
    const int selected = ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
    if (selected < 0 || static_cast<std::size_t>(selected) >= state.rows.size()) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(selected);
    return index < state.rows.size() ? &state.rows[index] : nullptr;
}

Browser* browser(HWND window) {
    return reinterpret_cast<Browser*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

void snapshot(Browser& state, bool reset, bool replacing_assets = false) {
    state.pending_asset.reset();
    if (const auto* row = selected_row(state)) {
        if (replacing_assets && state.mode_index == 6 && row->asset) {
            state.pending_asset = state.assets.assets[*row->asset].path;
            state.pending_selection.reset();
        } else {
            state.pending_selection = row->key;
        }
    }
    auto next = state.snapshot_provider ? state.snapshot_provider() : NativeDatabaseSnapshot{};
    state.previous = reset ? NativeDatabaseSnapshot{} : std::move(state.native);
    state.changes = database_variable_changes(state.previous, next);
    if (reset || (state.snapshot_provider &&
                  (state.previous.manager_address != next.manager_address || !next.available))) {
        state.history.clear();
        state.views = {};
        state.database_pane = Pane::fields;
        state.asset_panes.clear();
    }
    state.native = std::move(next);
    state.native_updated = GetTickCount64();
}

void reload(Browser& state, bool reset = false) {
    auto io = game_assets::database_io_snapshot();
    reset = reset || io.session != state.io.session || io.path != state.io.path;
    state.io = std::move(io);
    snapshot(state, reset, true);
    const auto requested =
        !state.requested_path.empty() ? state.requested_path
        : state.io.path.empty()
            ? database_asset_file(state.root, L"XFILES.HDB").value_or(std::filesystem::path{})
            : std::filesystem::path(state.io.path);
    state.stored = {};
    state.resource_strings_path.reset();
    state.database.reset();
    state.clips.reset();
    state.assets = {};
    state.manual_offset.reset();
    state.path = requested;
    state.rows.clear();
    ListView_SetItemCount(state.list, 0);
    std::string error;
    if (!requested.empty()) {
        try {
            state.database = game_assets::Database::load(requested);
            state.stored = game_assets::parse_database_index(state.database->bytes());
            state.clips = game_assets::ClipIndex::load(requested);
        } catch (const std::exception& failure) {
            error = failure.what();
        }
    }
    const auto labels = state.clips
                            ? std::span<const game_assets::ClipEntry>(state.clips->entries())
                            : std::span<const game_assets::ClipEntry>{};
    DatabaseAssetLimits limits;
    limits.selected_asset = state.requested_asset;
    state.assets = database_assets(state.root, labels, state.native, limits);
    if (state.pending_asset) {
        state.pending_selection = database_asset_find(state.assets, *state.pending_asset);
        state.pending_asset.reset();
    }
    const auto previous_filter = filter_class(state);
    const auto previous_asset_filter = state.filter_index;
    configure_filter(state);
    if (state.mode_index == 6) {
        state.filter_index = previous_asset_filter;
        SendMessageW(state.filter, CB_SETCURSEL, state.filter_index, 0);
    }
    if (state.mode_index != 6 && previous_filter) {
        for (LRESULT index = 0; index < SendMessageW(state.filter, CB_GETCOUNT, 0, 0); ++index) {
            if (SendMessageW(state.filter, CB_GETITEMDATA, index, 0) ==
                static_cast<LRESULT>(previous_filter)) {
                state.filter_index = static_cast<unsigned>(index);
                SendMessageW(state.filter, CB_SETCURSEL, index, 0);
                break;
            }
        }
    }
    update_database_table(state);
    populate(state);
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
}

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM value, LPARAM data) {
    auto* state = browser(window);
    if (!state) {
        return DefWindowProcW(window, message, value, data);
    }
    try {
        if (message == WM_SIZE) {
            layout(*state);
        } else if (message == WM_COMMAND) {
            const auto id = LOWORD(value), event = HIWORD(value);
            if (id == mode_id && event == CBN_SELCHANGE) {
                const auto selected = SendMessageW(state->mode, CB_GETCURSEL, 0, 0);
                const auto mode = SendMessageW(state->mode, CB_GETITEMDATA, selected, 0);
                if (mode < 0 || mode > 7) {
                    return 0;
                }
                switch_view(*state, static_cast<unsigned>(mode));
            } else if (id == filter_id && event == CBN_SELCHANGE) {
                state->filter_index =
                    static_cast<unsigned>(SendMessageW(state->filter, CB_GETCURSEL, 0, 0));
                populate(*state);

            } else if (id == follow_id) {
                follow(*state);
            } else if (id == fields_copy_id) {
                copy_field_table(*state);
            } else if (id == database_browse_id) {
                ListView_SetItemState(state->database_classes, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                browse_database_class(*state);
            } else if (id == asset_browse_id) {
                switch_view(*state, 6);
            } else if (id == database_view_id) {
                if (state->database) {
                    switch_view(*state, 7);
                }
            } else if (id == back_id && !state->history.empty()) {
                const auto destination = state->history.back();
                state->history.pop_back();
                state->views[state->mode_index] = current_view(*state);
                restore_view(*state, destination);
            } else if (id == preview_id) {
                const auto* row = selected_row(*state);
                if (row && row->asset) {
                    const auto& asset = state->assets.assets[*row->asset];
                    if (asset.present && asset.type == L"Text") {
                        select_pane(*state, Pane::text);
                        layout(*state);
                    } else if (asset.present && asset.type == L"Font") {
                        select_pane(*state, Pane::font);
                        layout(*state);
                    } else if (asset.present && asset.type == L"Localization") {
                        select_pane(*state, Pane::strings);
                        layout(*state);
                    } else if (asset.present && asset.type == L"Database" && state->database &&
                               lower(asset.physical_path.wstring()) ==
                                   lower(state->path.wstring())) {
                        ListView_SetItemState(state->database_classes, -1, 0,
                                              LVIS_SELECTED | LVIS_FOCUSED);
                        browse_database_class(*state);
                    } else if (asset.present &&
                               lower(asset.path.extension().wstring()) == L".pff") {
                        const auto file = database_asset_file(state->root, asset.path);
                        if (!file) {
                            throw std::runtime_error(
                                "The archive is missing or its path is unsafe");
                        }
                        if (state->archive_window && *state->archive_window) {
                            DestroyWindow(*state->archive_window);
                        }
                        state->archive_window = open_pff_browser(
                            window,
                            reinterpret_cast<HMODULE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)),
                            reinterpret_cast<HFONT>(SendMessageW(state->mode, WM_GETFONT, 0, 0)),
                            *file);
                    } else if (asset.present && state->open_asset &&
                               (asset.previewable ||
                                (asset.type == L"Database" && !state->snapshot_provider))) {
                        state->open_asset(asset.path);
                    }
                }
            } else if (id == strings_search_id && event == EN_CHANGE && !state->rebuilding) {
                filter_strings(*state);
            } else if (id == strings_copy_id) {
                copy_string(*state);
            } else if (id == search_id && event == EN_CHANGE && !state->rebuilding) {
                state->manual_offset.reset();
                populate(*state);
            } else if (id == refresh_id) {
                reload(*state);
            } else if (id == jump_id && state->database) {
                const auto* row = selected_row(*state);
                if (row && row->asset) {
                    return 0;
                }
                const auto input = text(state->offset);
                std::size_t consumed = 0;
                const auto offset = std::stoull(input, &consumed, 16);
                if (consumed != input.size() || offset >= state->database->size()) {
                    throw std::runtime_error("Enter a hexadecimal offset inside the database");
                }
                state->manual_offset = static_cast<std::size_t>(offset);
                select_pane(*state, Pane::raw);
                layout(*state);
                describe(*state);
            }
        } else if (message == WM_NOTIFY) {
            auto* notification = reinterpret_cast<NMHDR*>(data);
            if (table_notification(*state, notification)) {
                return 0;
            }
            if (string_notification(*state, notification)) {
                return 0;
            }
            if (notification->hwndFrom == state->tabs && notification->code == TCN_SELCHANGE) {
                select_pane(*state, selected_pane(*state));
                layout(*state);
            } else if (notification->hwndFrom == state->list &&
                       notification->code == LVN_COLUMNCLICK) {
                const int column = reinterpret_cast<NMLISTVIEW*>(data)->iSubItem;
                if (column >= 0 && column < 4) {
                    state->sort_descending =
                        column == state->sort_column ? !state->sort_descending : false;
                    state->sort_column = column;
                    populate(*state);
                }
            } else if ((notification->hwndFrom == state->list ||
                        notification->hwndFrom == state->relations) &&
                       notification->code == LVN_GETDISPINFOW) {
                auto* item = &reinterpret_cast<NMLVDISPINFOW*>(data)->item;
                if ((item->mask & LVIF_TEXT) && item->iItem >= 0 && item->iSubItem >= 0 &&
                    item->iSubItem < 4) {
                    const auto index = static_cast<std::size_t>(item->iItem),
                               column = static_cast<std::size_t>(item->iSubItem);
                    const std::wstring* content = nullptr;
                    if (notification->hwndFrom == state->list && index < state->rows.size()) {
                        content = &state->rows[index].columns[column];
                    } else if (notification->hwndFrom == state->relations &&
                               index < state->links.size()) {
                        content = &state->links[index].columns[column];
                    }
                    if (content) {
                        wcsncpy_s(item->pszText, static_cast<std::size_t>(item->cchTextMax),
                                  content->c_str(), _TRUNCATE);
                    }
                }
            } else if (notification->hwndFrom == state->list &&
                       notification->code == LVN_ITEMCHANGED && !state->rebuilding) {
                const auto* change = reinterpret_cast<const NMLISTVIEW*>(data);
                if ((change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED)) {
                    state->manual_offset.reset();
                }
                describe(*state);
            } else if (notification->hwndFrom == state->relations &&
                       notification->code == LVN_ITEMCHANGED && !state->rebuilding) {
                const int selected = ListView_GetNextItem(state->relations, -1, LVNI_SELECTED);
                const auto* link = selected >= 0 && std::size_t(selected) < state->links.size()
                                       ? &state->links[static_cast<std::size_t>(selected)]
                                       : nullptr;
                EnableWindow(state->follow, link && (link->destination.object ||
                                                     !link->destination.asset.empty()));
            } else if (notification->hwndFrom == state->relations &&
                       (notification->code == NM_DBLCLK ||
                        (notification->code == LVN_KEYDOWN &&
                         reinterpret_cast<NMLVKEYDOWN*>(data)->wVKey == VK_RETURN))) {
                follow(*state);
            } else if (notification->hwndFrom == state->list &&
                       (notification->code == NM_DBLCLK ||
                        (notification->code == LVN_KEYDOWN &&
                         reinterpret_cast<NMLVKEYDOWN*>(data)->wVKey == VK_RETURN))) {
                if (const auto* row = selected_row(*state)) {
                    if (row->asset && state->mode_index != 6) {
                        navigate(*state, {{}, state->assets.assets[*row->asset].path}, true);
                    } else if (row->asset && IsWindowEnabled(state->preview)) {
                        SendMessageW(window, WM_COMMAND, preview_id, 0);
                    } else {
                        select_pane(*state, row->object ? Pane::links : Pane::overview);
                        layout(*state);
                    }
                }
            } else if (notification->hwndFrom == state->properties &&
                       notification->code == TVN_KEYDOWN) {
                const auto* key = reinterpret_cast<NMTVKEYDOWN*>(data);
                if (key->wVKey == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    copy_property(*state);
                }
            }
        } else if (message == WM_DRAWITEM && value == font_id && state->font_preview) {
            const auto& draw = *reinterpret_cast<const DRAWITEMSTRUCT*>(data);
            state->font_preview->paint(draw.hDC, draw.rcItem);
            if (state->font_preview->description().empty()) {
                RECT bounds = draw.rcItem;
                DrawTextW(draw.hDC, L"Font file is missing or its path is unsafe.", -1, &bounds,
                          DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            }
            return TRUE;
        } else if (message == WM_CTLCOLORSTATIC) {
            SetBkColor(reinterpret_cast<HDC>(value), GetSysColor(COLOR_BTNFACE));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        } else if (message == WM_NCDESTROY) {
            if (state->archive_window && *state->archive_window) {
                DestroyWindow(*state->archive_window);
            }
            DeleteObject(state->fixed_font);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            delete state;
        }
    } catch (const std::exception& error) {
        SetWindowTextA(state->status, error.what());
    }
    return DefWindowProcW(window, message, value, data);
}
}

namespace devtools {
HWND create_database_browser(HWND parent, HMODULE module, HFONT font,
                             const std::filesystem::path& root,
                             std::function<void(const std::filesystem::path&)> open_asset,
                             std::function<NativeDatabaseSnapshot()> snapshot_provider,
                             const std::filesystem::path& hdb_path, bool asset_view,
                             const std::filesystem::path& selected_asset) {
    using namespace database_browser;
    WNDCLASSW type{};
    type.lpfnWndProc = procedure;
    type.hInstance = module;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = browser_class;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("Cannot register database browser");
    }
    auto state = std::make_unique<Browser>();
    state->root = root;
    state->open_asset = std::move(open_asset);
    state->snapshot_provider = std::move(snapshot_provider);
    state->requested_path = hdb_path;
    state->requested_asset = selected_asset;
    state->mode_index = state->snapshot_provider ? 0u : asset_view ? 6u : 7u;
    const auto window =
        CreateWindowExW(WS_EX_CONTROLPARENT, browser_class, L"HDB database",
                        WS_CHILD | WS_CLIPCHILDREN, 0, 0, 1, 1, parent, nullptr, module, nullptr);
    if (!window) {
        throw std::runtime_error("Cannot create database browser");
    }
    auto* view = state.release();
    view->window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
    try {
        create_controls(*view, module, font, parent);
        layout(*view);
        try {
            reload(*view);
        } catch (const std::exception& error) {
            SetWindowTextA(view->status, error.what());
        }
        if (!selected_asset.empty()) {
            navigate(*view, {{}, selected_asset}, false);
        }
        return window;
    } catch (...) {
        DestroyWindow(window);
        throw;
    }
}

void update_database_browser(HWND window) {
    using namespace database_browser;
    auto* state = browser(window);
    if (!state || !IsWindowVisible(window) || GetTickCount64() - state->updated < 500) {
        return;
    }
    state->updated = GetTickCount64();
    auto io = game_assets::database_io_snapshot();
    const bool reopened = io.session != state->io.session || io.path != state->io.path;
    const bool changed = io.total_reads != state->io.total_reads || io.path != state->io.path ||
                         io.open != state->io.open;
    state->io = std::move(io);
    if (reopened) {
        try {
            reload(*state, true);
        } catch (const std::exception& error) {
            SetWindowTextA(state->status, error.what());
        }
    } else if (SendMessageW(state->auto_refresh, BM_GETCHECK, 0, 0) == BST_CHECKED &&
               GetTickCount64() - state->native_updated >= 2000) {
        snapshot(*state, false);
        database_asset_refresh_references(state->assets, state->native);
        populate(*state);
    } else if (changed) {
        populate(*state);
    }
}
}
