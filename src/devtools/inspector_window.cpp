#include "inspector_internal.h"
#include "state_capture.h"
#include "subtitle_editor.h"
#include "database/browser.h"
#include "enhancements/game_ui.h"
#include "platform/tool_cursor.h"
#include "platform/tool_theme.h"
#include <uxtheme.h>
#include <algorithm>
#include <stdexcept>

namespace devtools::inspector {
void show_view();

LRESULT CALLBACK preview_paint(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR,
                               DWORD_PTR) {
    return message == WM_ERASEBKGND ? 1 : DefSubclassProc(window, message, value, data);
}

LRESULT CALLBACK child_keys(HWND window, UINT message, WPARAM key, LPARAM data, UINT_PTR,
                            DWORD_PTR) {
    if (message == WM_KEYDOWN && key == VK_TAB) {
        SetFocus(GetNextDlgTabItem(state.window, window, (GetKeyState(VK_SHIFT) & 0x8000) != 0));
        return 0;
    }
    if (message == WM_KEYDOWN && key == VK_ESCAPE) {
        SendMessageW(state.window, WM_CLOSE, 0, 0);
        return 0;
    }
    if (message == WM_KEYDOWN && (window == state.state_text || window == state.details) &&
        key == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        copy_state_item(window);
        return 0;
    }
    if (message == WM_KEYDOWN && window == state.state_variables && key == 'C' &&
        (GetKeyState(VK_CONTROL) & 0x8000)) {
        copy_state_variable(window, state.state_variable_view, state.snapshot);
        return 0;
    }
    if (message == WM_KEYDOWN && window == state.state_history_list && key == 'C' &&
        (GetKeyState(VK_CONTROL) & 0x8000)) {
        copy_state_history(true);
        return 0;
    }
    if (message == WM_KEYDOWN && state.showing_state && key == 'F' &&
        (GetKeyState(VK_CONTROL) & 0x8000)) {
        SetFocus(state.state_search);
        SendMessageW(state.state_search, EM_SETSEL, 0, -1);
        return 0;
    }
    return DefSubclassProc(window, message, key, data);
}

HWND child(const wchar_t* type, const wchar_t* caption, DWORD style, int id) {
    const auto control =
        CreateWindowExW(0, type, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, state.window,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), state.module, nullptr);
    if (!control) {
        throw std::runtime_error("Cannot create inspector control");
    }
    SetWindowTheme(control, L"", L"");
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), FALSE);

    if (style & WS_TABSTOP) {
        SetWindowSubclass(control, child_keys, 1, 0);
    }
    return control;
}

void layout() {
    RECT r{};
    GetClientRect(state.window, &r);
    const int width = r.right, height = r.bottom, split = width / 2, right = split + 20,
              detail_width = width - right - 20;
    MoveWindow(state.live, 20, 18, 144, 34, TRUE);
    MoveWindow(state.library, 170, 18, 130, 34, TRUE);
    MoveWindow(state.search_label, 20, 70, 62, 24, TRUE);
    MoveWindow(state.search, 86, 66, split - 86, 30, TRUE);
    MoveWindow(state.game_state, 306, 18, 130, 34, TRUE);
    MoveWindow(state.database_button, 442, 18, 158, 34, TRUE);
    MoveWindow(state.database, 20, 66, width - 40, height - 120, TRUE);
    MoveWindow(state.filenames, 20, 100, 94, 24, TRUE);
    MoveWindow(state.labels, 118, 100, 85, 24, TRUE);
    MoveWindow(state.notes_search, 207, 100, 82, 24, TRUE);
    MoveWindow(state.captions, 293, 100, 100, 24, TRUE);
    MoveWindow(state.group, 20, 130, 152, 240, TRUE);
    MoveWindow(state.place, 178, 130, std::max(130, split - 314), 260, TRUE);
    MoveWindow(state.compact, split - 128, 130, 128, 28, TRUE);
    MoveWindow(state.coverage, 20, 164, split - 20, 220, TRUE);
    MoveWindow(state.list, 20, 200, split - 20, height - 254, TRUE);
    layout_state_controls(width, height);
    const int title_left = std::max(right, 620);
    MoveWindow(state.title, title_left, 20, width - title_left - 20, 32, TRUE);
    MoveWindow(state.preview, right, 66, detail_width, 180, TRUE);
    MoveWindow(state.clock, right, 252, detail_width, 24, TRUE);
    MoveWindow(state.play, right, 280, 118, 30, TRUE);
    MoveWindow(state.stop, right + 124, 280, 66, 30, TRUE);
    MoveWindow(state.subtitle, right + 196, 280, 110, 30, TRUE);
    MoveWindow(state.seek, right + 312, 280, std::max(40, detail_width - 312), 30, TRUE);
    MoveWindow(state.caption, right, 314, detail_width, 38, TRUE);
    MoveWindow(state.details, right, 355, detail_width, 123, TRUE);
    MoveWindow(state.label_heading, right, 483, detail_width, 22, TRUE);
    MoveWindow(state.label, right, 508, detail_width, 28, TRUE);
    MoveWindow(state.notes_heading, right, 540, detail_width, 22, TRUE);
    MoveWindow(state.notes, right, 565, detail_width, std::max(36, height - 665), TRUE);
    MoveWindow(state.save, right, height - 86, 154, 32, TRUE);
    MoveWindow(state.status, 20, height - 38, width - 40, 30, TRUE);
    const int clip_width = state.compact_view ? 128 : 222;
    ListView_SetColumnWidth(state.list, 0, clip_width);
    ListView_SetColumnWidth(state.list, 1, std::max(100, split - clip_width - 134));
    ListView_SetColumnWidth(state.list, 2, 110);
}

void create_controls() {
    platform::ToolTheme theme(state.module);
    INITCOMMONCONTROLSEX controls{sizeof(controls),
                                  ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        throw std::runtime_error("Cannot initialize developer controls");
    }
    state.font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Segoe UI");
    state.heading = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                                0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    state.live = child(L"BUTTON", L"Live movies",
                       BS_PUSHLIKE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, live_id);
    state.library =
        child(L"BUTTON", L"Library", BS_PUSHLIKE | BS_AUTORADIOBUTTON | WS_TABSTOP, library_id);
    SendMessageW(state.live, BM_SETCHECK, BST_CHECKED, 0);
    state.game_state = child(L"BUTTON", L"Game state",
                             BS_PUSHLIKE | BS_AUTORADIOBUTTON | WS_TABSTOP, game_state_id);
    state.database_button = child(L"BUTTON", L"Database && assets",
                                  BS_PUSHLIKE | BS_AUTORADIOBUTTON | WS_TABSTOP, database_id);
    create_state_controls();
    state.filenames = child(L"BUTTON", L"Filename", BS_AUTOCHECKBOX | WS_TABSTOP, filename_id);
    state.labels = child(L"BUTTON", L"Labels", BS_AUTOCHECKBOX | WS_TABSTOP, labels_id);
    state.notes_search = child(L"BUTTON", L"Notes", BS_AUTOCHECKBOX | WS_TABSTOP, notes_search_id);
    state.captions = child(L"BUTTON", L"Captions", BS_AUTOCHECKBOX | WS_TABSTOP, captions_id);
    for (auto control : {state.filenames, state.labels, state.notes_search}) {
        SendMessageW(control, BM_SETCHECK, BST_CHECKED, 0);
    }
    state.group =
        child(L"COMBOBOX", L"Media group", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, group_id);
    for (const auto* name : {L"All groups", L"XN", L"XV", L"XG", L"XS", L"XT", L"Archives",
                             L"Action choices", L"Emotions"}) {
        SendMessageW(state.group, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    SendMessageW(state.group, CB_SETCURSEL, 0, 0);
    state.place =
        child(L"COMBOBOX", L"Place", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, place_id);
    state.coverage = child(L"COMBOBOX", L"Caption coverage",
                           CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, coverage_id);
    for (const auto* name :
         {L"All caption coverage", L"Has captions", L"No captions", L"Unreadable caption data"}) {
        SendMessageW(state.coverage, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    SendMessageW(state.coverage, CB_SETCURSEL, 0, 0);
    state.compact = child(L"BUTTON", L"Compact rows", BS_AUTOCHECKBOX | WS_TABSTOP, compact_id);
    SendMessageW(state.compact, BM_SETCHECK, BST_CHECKED, 0);
    state.search_label = child(L"STATIC", L"Search");
    state.search = child(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, search_id);
    SendMessageW(state.search, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Search filename, place, label or notes"));
    state.list =
        child(WC_LISTVIEWW, L"",
              WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, list_id);
    ListView_SetExtendedListViewStyle(state.list,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    set_list_density();
    int column = 0;
    for (const auto* name : {L"Clip", L"Label / place", L"Activity"}) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT;
        c.pszText = const_cast<LPWSTR>(name);
        ListView_InsertColumn(state.list, column++, &c);
    }
    state.title = child(L"STATIC", L"Select a clip", SS_ENDELLIPSIS);
    SendMessageW(state.title, WM_SETFONT, reinterpret_cast<WPARAM>(state.heading), FALSE);
    state.preview = child(L"STATIC", L"", SS_OWNERDRAW);
    SetWindowSubclass(state.preview, preview_paint, 1, 0);
    state.clock = child(L"STATIC", L"");
    state.caption = child(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_TABSTOP);
    state.play = child(L"BUTTON", L"Play preview", BS_PUSHBUTTON | WS_TABSTOP, play_id);
    state.stop = child(L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, stop_id);
    state.subtitle = child(L"BUTTON", L"Subtitles...", BS_PUSHBUTTON | WS_TABSTOP, subtitle_id);
    state.seek = child(TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, seek_id);
    SendMessageW(state.seek, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
    state.details = child(WC_TREEVIEWW, L"Clip details",
                          WS_BORDER | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                              TVS_SHOWSELALWAYS | TVS_FULLROWSELECT);
    TreeView_SetBkColor(state.details, GetSysColor(COLOR_BTNFACE));
    state.label_heading = child(L"STATIC", L"Your label");
    state.label = child(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, label_id);
    SendMessageW(state.label, EM_SETLIMITTEXT, 1024, 0);
    state.notes_heading = child(L"STATIC", L"Your notes");
    state.notes =
        child(L"EDIT", L"",
              WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
              notes_id);
    SendMessageW(state.notes, EM_SETLIMITTEXT, 16000, 0);
    state.save = child(L"BUTTON", L"Save label and notes", BS_PUSHBUTTON | WS_TABSTOP, save_id);
    state.status = child(L"STATIC", L"");
    state.catalog.load();
    state.artwork = std::make_unique<ArtworkCache>(state.catalog.root);
    SendMessageW(state.place, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All places"));
    SendMessageW(state.place, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No place label"));
    for (const auto& place : state.catalog.places()) {
        SendMessageW(state.place, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(place.c_str()));
    }
    SendMessageW(state.place, CB_SETCURSEL, 0, 0);
    try {
        if (state.catalog.notes_path.empty()) {
            throw std::runtime_error("Game database unavailable. Notes cannot be saved.");
        }
        state.annotations = ClipNotes::load(state.catalog.notes_path);
        state.notes_ok = true;
    } catch (const std::exception& error) {
        state.prompting = true;

        struct Reset {
            ~Reset() {
                state.prompting = false;
            }
        } reset;

        platform::ToolCursor cursor(state.game);
        MessageBoxA(state.window, error.what(), "Clip notes unavailable", MB_OK | MB_ICONWARNING);
    }
    EnableWindow(state.label, FALSE);
    EnableWindow(state.notes, FALSE);
    EnableWindow(state.save, FALSE);
    layout();
    show_view();
    SetTimer(state.window, 1, 33, nullptr);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM value, LPARAM data) {
    try {
        if (message == WM_CREATE) {
            state.window = window;
            create_controls();
            return 0;
        }
        if (message == WM_SETCURSOR) {
            platform::tool_cursor(state.game, true);
        }
        if (message == WM_ACTIVATE) {
            platform::tool_cursor(state.game, LOWORD(value) != WA_INACTIVE);
        }
        if (message == WM_SIZE) {
            layout();
            return 0;
        }
        if (message == WM_GETMINMAXINFO) {
            reinterpret_cast<MINMAXINFO*>(data)->ptMinTrackSize = {1000, 810};
            return 0;
        }
        if (message == WM_CTLCOLORSTATIC) {
            SetBkColor(reinterpret_cast<HDC>(value), GetSysColor(COLOR_BTNFACE));
            SetTextColor(reinterpret_cast<HDC>(value), GetSysColor(COLOR_BTNTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        if (message == WM_TIMER && !state.prompting) {
            tick_preview();
            update_artwork();
            return 0;
        }
        if (message == WM_HSCROLL && reinterpret_cast<HWND>(data) == state.seek && state.player) {
            state.player->seek(state.player->duration() *
                               SendMessageW(state.seek, TBM_GETPOS, 0, 0) / 1000);
            tick_preview();
            return 0;
        }
        if (message == WM_DRAWITEM) {
            const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(data);
            if (item->hwndItem == state.preview) {
                const auto* movie = selected_movie();
                paint_frame(item->hDC, item->rcItem,
                            state.player ? &state.player->frame()
                            : movie      ? &movie->preview
                                         : nullptr,
                            state.player ? state.player->caption()
                            : movie      ? movie->caption
                                         : L"");
                return TRUE;
            }
        }
        if (message == WM_COMMAND && !state.rebuilding) {
            const auto id = LOWORD(value), event = HIWORD(value);
            if (id == IDCANCEL) {
                SendMessageW(window, WM_CLOSE, 0, 0);
                return 0;
            }
            if (id == play_id || id == subtitle_id) {
                SetTimer(window, 1, 33, nullptr);
                if (!state.selected.empty()) {
                    if (!state.player) {
                        state.player =
                            std::make_unique<Preview>(state.catalog.root, state.selected);
                    }
                    if (id == subtitle_id) {
                        state.prompting = true;

                        struct Reset {
                            ~Reset() {
                                state.prompting = false;
                            }
                        } reset;

                        if (!leave_notes()) {
                            return 0;
                        }
                        std::vector<std::filesystem::path> movies;
                        for (const auto& row : state.rows) {
                            if (state.catalog.installed_keys.contains(catalog_key(row.path)) &&
                                std::find(movies.begin(), movies.end(), row.path) == movies.end()) {
                                movies.push_back(row.path);
                            }
                        }
                        edit_subtitles(state.window, state.game, state.module, state.catalog.root,
                                       state.selected, state.player, movies);
                        state.rebuilding = true;
                        const auto note = state.annotations.get(state.selected);
                        set_text(state.label, note ? note->label : L"");
                        set_text(state.notes, note ? note->notes : L"");
                        state.rebuilding = false;
                        state.selected_movie = 0;
                        for (const auto& row : state.rows) {
                            if (row.path == state.selected) {
                                state.selected_movie = row.movie;
                            }
                        }
                        populate();
                    } else if (state.player->playing()) {
                        state.player->pause();
                    } else {
                        state.player->play();
                    }
                    describe();
                    tick_preview();
                }
            }
            if (id == stop_id && state.player) {
                state.player->pause();
                state.player->seek(0);
                tick_preview();
            }
            if (id == compact_id) {
                state.compact_view = SendMessageW(state.compact, BM_GETCHECK, 0, 0) != 0;
                set_list_density();
                state.rows.clear();
                populate();
                layout();
            }
            if ((id == group_id || id == place_id || id == coverage_id) && event == CBN_SELCHANGE) {
                if (id == coverage_id && SendMessageW(state.coverage, CB_GETCURSEL, 0, 0) > 0) {
                    start_caption_index();
                }
                populate();
            }
            if (state_command(id, event)) {
                return 0;
            }
            if (id == captions_id && SendMessageW(state.captions, BM_GETCHECK, 0, 0)) {
                start_caption_index();
            }
            if ((id == search_id && event == EN_CHANGE) ||
                (id >= filename_id && id <= captions_id)) {
                populate();
            }
            if ((id == label_id || id == notes_id) && event == EN_CHANGE) {
                state.dirty = true;
                update_save();
            }
            if (id == save_id && save_notes()) {
                populate();
            }
            if (id == live_id || id == library_id || id == game_state_id || id == database_id) {
                if (leave_notes()) {
                    state.player.reset();
                    state.showing_state = id == game_state_id;
                    state.showing_database = id == database_id;
                    if (state.showing_database && !state.database) {
                        state.database = create_database_browser(
                            state.window, state.module, state.font, state.catalog.root,
                            open_database_asset, [] {
                                return inspect_database(enhancements::game::executable_image(),
                                                        enhancements::game::edition());
                            });
                        layout();
                    }
                    state.browsing = id == library_id;
                    state.selected.clear();
                    state.selected_movie = 0;
                    show_view();
                    if (state.showing_state) {
                        state.snapshot = inspect_game();
                        collect_state_history(state.state_history, state.snapshot, true);
                        state.state_updated = GetTickCount64();
                        update_game_state_view();
                    } else if (!state.showing_database) {
                        populate();
                        describe();
                    }
                }
                SendMessageW(state.live, BM_SETCHECK,
                             !state.browsing && !state.showing_state && !state.showing_database
                                 ? BST_CHECKED
                                 : BST_UNCHECKED,
                             0);
                SendMessageW(state.library, BM_SETCHECK,
                             state.browsing ? BST_CHECKED : BST_UNCHECKED, 0);
                SendMessageW(state.game_state, BM_SETCHECK,
                             state.showing_state ? BST_CHECKED : BST_UNCHECKED, 0);
                SendMessageW(state.database_button, BM_SETCHECK,
                             state.showing_database ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            return 0;
        }
        if (message == WM_NOTIFY && !state.rebuilding) {
            const auto* change = reinterpret_cast<NMLISTVIEW*>(data);
            if (state_notify(*change)) {
                return 0;
            }
            if (change->hdr.hwndFrom == state.list && change->hdr.code == LVN_ITEMCHANGING &&
                (change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED)) {
                if (!leave_notes()) {
                    return TRUE;
                }
            }
            if (change->hdr.hwndFrom == state.list && change->hdr.code == LVN_ITEMCHANGED &&
                (change->uNewState & LVIS_SELECTED) && change->iItem >= 0 &&
                static_cast<std::size_t>(change->iItem) < state.rows.size()) {
                select(state.rows[change->iItem]);
            }
        }
        if (message == WM_CLOSE) {
            if (leave_notes()) {
                DestroyWindow(window);
            }
            return 0;
        }
        if (message == WM_DESTROY) {
            release_state_capture();
            state.player.reset();
            state.caption_index.reset();
            state.artwork.reset();
            release_hotspots();
            platform::tool_cursor(state.game, false);
            state.window = nullptr;
            state.database = nullptr;
            return 0;
        }
    } catch (const std::exception& error) {
        if (message == WM_TIMER) {
            if (state.player) {
                state.player->pause();
            }
            KillTimer(window, 1);
            SetWindowTextA(state.status, error.what());
            return 0;
        }
        state.prompting = true;

        struct Reset {
            ~Reset() {
                state.prompting = false;
            }
        } reset;

        platform::ToolCursor cursor(state.game);
        MessageBoxA(window, error.what(), "The X-Files inspector", MB_OK | MB_ICONERROR);
        if (message == WM_CREATE) {
            return -1;
        }
    }
    return DefWindowProcW(window, message, value, data);
}

}
