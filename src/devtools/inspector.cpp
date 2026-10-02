#include "inspector.h"
#include "inspector_internal.h"
#include "message_pump.h"
#include "diagnostics/game_context.h"
#include "clip_catalog.h"
#include "clip_notes.h"
#include "playback/inspection.h"
#include "platform/tool_cursor.h"
#include "preview.h"
#include "caption_index.h"
#include "game_state.h"
#include "state_capture.h"
#include "subtitle_editor.h"
#include "database/browser.h"
#include "database/assets.h"

#include <commctrl.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace devtools::inspector {
thread_local State state;

std::wstring text(HWND control) {
    std::wstring result(GetWindowTextLengthW(control) + 1, L'\0');
    result.resize(GetWindowTextW(control, result.data(), static_cast<int>(result.size())));
    return result;
}

void set_text(HWND control, const std::wstring& value) {
    if (text(control) != value) {
        SetWindowTextW(control, value.c_str());
    }
}

const playback::MovieSnapshot* selected_movie() {
    if (state.selected.empty()) {
        return nullptr;
    }
    for (const auto& item : state.movies) {
        if (state.browsing ? catalog_key(item.path) == catalog_key(state.selected)
                           : item.id == state.selected_movie) {
            return &item;
        }
    }
    return nullptr;
}

std::wstring timestamp(std::uint64_t ticks, std::uint32_t scale) {
    if (!scale) {
        return L"unknown";
    }
    const auto ms = ticks * 1000 / scale;
    wchar_t result[64]{};
    swprintf_s(result, L"%llu:%02llu.%01llu", ms / 60000, ms / 1000 % 60, ms / 100 % 10);
    return result;
}

std::wstring activity(const playback::MovieSnapshot& movie) {
    if (!movie.video) {
        return movie.audio ? L"Audio" : L"No enabled tracks";
    }
    if (!movie.last_draw) {
        return L"Not drawn yet";
    }
    if (!movie.active) {
        return L"Inactive";
    }
    if (!movie.playing) {
        return L"Still / stopped";
    }
    return GetTickCount64() - movie.last_draw < 1000 ? L"Drawing frames" : L"Playing";
}

void describe() {
    const auto note = state.selected.empty() ? std::nullopt : state.annotations.get(state.selected);
    set_text(state.title,
             note && !note->label.empty() ? note->label : state.selected.generic_wstring());
    update_preview_details();
    EnableWindow(state.play, !state.selected.empty());
    const auto* movie = selected_movie();
    const bool sound_only = state.player ? state.player->has_audio() && !state.player->has_video()
                            : movie      ? movie->audio && !movie->video
                                         : false;
    EnableWindow(state.subtitle, !state.selected.empty() && !sound_only);
    InvalidateRect(state.preview, nullptr, FALSE);
}

void update_save() {
    EnableWindow(state.save, state.dirty && state.notes_ok);
    set_text(state.status, state.dirty ? L"Unsaved label or notes" : state.catalog.status);
}

bool save_notes() {
    try {
        auto changed = state.annotations;
        changed.set(state.selected, text(state.label), text(state.notes));
        changed.save(state.catalog.notes_path);
        state.annotations = std::move(changed);
        state.dirty = false;
        update_save();
        describe();
        return true;
    } catch (const std::exception& error) {
        platform::ToolCursor cursor(state.game);
        MessageBoxA(state.window, error.what(), "Cannot save clip notes", MB_OK | MB_ICONERROR);
        return false;
    }
}

bool leave_notes() {
    if (!state.dirty) {
        return true;
    }
    const auto previous = state.prompting;
    state.prompting = true;

    struct Reset {
        bool previous;

        ~Reset() {
            state.prompting = previous;
        }
    } reset{previous};

    platform::ToolCursor cursor(state.game);
    const auto result =
        MessageBoxW(state.window, L"Save your changes to this clip's label and notes?",
                    L"Clip notes", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (result == IDCANCEL || (result == IDYES && !save_notes())) {
        return false;
    }
    state.dirty = false;
    return true;
}

void select(const Row& row) {
    state.player.reset();
    state.selected = row.path;
    diagnostics::record_tool_context(
        std::wstring(state.browsing ? L"select library " : L"select live ") +
        row.path.generic_wstring());
    state.selected_movie = row.movie;
    const auto note = row.path.empty() ? std::nullopt : state.annotations.get(row.path);
    state.rebuilding = true;
    set_text(state.label, note ? note->label : L"");
    set_text(state.notes, note ? note->notes : L"");
    state.rebuilding = false;
    state.dirty = false;
    EnableWindow(state.label, state.notes_ok && !row.path.empty());
    EnableWindow(state.notes, state.notes_ok && !row.path.empty());
    update_save();
    if (state.browsing && !row.path.empty()) {
        try {
            state.player = std::make_unique<Preview>(state.catalog.root, row.path);
        } catch (const std::exception& error) {
            SetWindowTextA(state.status, error.what());
        }
    }
    describe();
    tick_preview();
}

void start_caption_index() {
    if (!state.caption_index) {
        state.caption_index = std::make_unique<CaptionIndex>();
    }
    state.caption_index->start(state.catalog.root, state.catalog.paths);
    state.caption_generation = media::subtitles::install_generation();
}

void tick_preview() {
    if (state.player) {
        state.player->update();
        set_text(state.clock, L"Preview  " + timestamp(state.player->time(), 1000) + L" / " +
                                  timestamp(state.player->duration(), 1000));
        set_text(state.play, state.player->playing() ? L"Pause preview" : L"Play preview");
        if (GetFocus() != state.caption) {
            set_text(state.caption, state.player->caption());
        }
        SendMessageW(state.seek, TBM_SETPOS, TRUE,
                     static_cast<LPARAM>(state.player->duration() ? state.player->time() * 1000 /
                                                                        state.player->duration()
                                                                  : 0));
        InvalidateRect(state.preview, nullptr, FALSE);
    } else if (const auto* movie = selected_movie()) {
        set_text(state.clock, activity(*movie) + L"  " +
                                  timestamp(playback::inspect_time(movie->id), movie->timescale) +
                                  L" / " + timestamp(movie->duration, movie->timescale));
        if (GetFocus() != state.caption) {
            set_text(state.caption, movie->caption);
        }
        set_text(state.play, L"Play preview");
    } else {
        set_text(state.clock, L"");
        set_text(state.caption, L"");
        set_text(state.play, L"Play preview");
    }
    update_preview_details();
    EnableWindow(state.stop, state.player != nullptr);
    EnableWindow(state.seek, state.player != nullptr);
}

void show_view() {
    for (auto window : {state.coverage,      state.group,    state.place,         state.compact,
                        state.search_label,  state.search,   state.filenames,     state.labels,
                        state.notes_search,  state.captions, state.list,          state.title,
                        state.preview,       state.clock,    state.play,          state.stop,
                        state.subtitle,      state.seek,     state.caption,       state.details,
                        state.label_heading, state.label,    state.notes_heading, state.notes,
                        state.save}) {
        ShowWindow(window, state.showing_state || state.showing_database ? SW_HIDE : SW_SHOW);
    }
    show_state_controls(state.showing_state);
    ShowWindow(state.database, state.showing_database ? SW_SHOW : SW_HIDE);
    if (state.showing_database) {
        set_text(state.status, L"Browse database tables or asset files. Values are read-only.");
    }
    if (state.showing_state) {
        set_text(state.status,
                 L"Hold Live updates and enable editing. Enter a decimal value or 'A'. "
                 L"Changes can affect gameplay and later saves.");
    }
}

void open_database_asset(const std::filesystem::path& requested) {
    const auto path = database_asset_path(requested);
    if (!path || !database_asset_previewable(*path) ||
        !database_asset_file(state.catalog.root, *path) || !leave_notes()) {
        return;
    }
    state.player.reset();
    state.showing_state = false;
    state.showing_database = false;
    state.browsing = true;
    state.selected.clear();
    state.selected_movie = 0;
    if (std::none_of(state.catalog.paths.begin(), state.catalog.paths.end(),
                     [&](const auto& item) { return catalog_key(item) == catalog_key(*path); })) {
        state.catalog.paths.push_back(*path);
    }
    state.catalog.installed_keys.insert(catalog_key(*path));
    state.selected = *path;
    state.rebuilding = true;
    set_text(state.search, L"");
    for (auto control : {state.group, state.place, state.coverage}) {
        SendMessageW(control, CB_SETCURSEL, 0, 0);
    }
    state.rebuilding = false;
    show_view();
    populate();
    select({*path, 0});
    state.rebuilding = true;
    for (std::size_t row = 0; row < state.rows.size(); ++row) {
        const bool selected = catalog_key(state.rows[row].path) == catalog_key(*path);
        ListView_SetItemState(state.list, static_cast<int>(row), selected ? LVIS_SELECTED : 0,
                              LVIS_SELECTED);
        if (selected) {
            ListView_EnsureVisible(state.list, static_cast<int>(row), FALSE);
        }
    }
    state.rebuilding = false;
    SendMessageW(state.live, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(state.library, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(state.game_state, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(state.database_button, BM_SETCHECK, BST_UNCHECKED, 0);
    SetFocus(state.list);
}

void dispose_resources() {
    if (state.images) {
        ImageList_Destroy(state.images);
    }
    if (state.font) {
        DeleteObject(state.font);
    }
    if (state.heading) {
        DeleteObject(state.heading);
    }
    if (state.module) {
        UnregisterClassW(class_name, state.module);
    }
}

void open(HWND game) {
    if (state.window) {
        ShowWindow(state.window, SW_SHOWNORMAL);
        SetForegroundWindow(state.window);
        return;
    }
    dispose_resources();
    auto variables = std::move(state.state_variable_view);
    auto history = std::move(state.state_history);
    state = State{};
    state.state_variable_view = std::move(variables);
    state.state_variable_view.cells.clear();
    state.state_variable_view.rows.clear();
    state.state_variable_view.keys.clear();
    state.state_history = std::move(history);
    state.game = game;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(window_proc), &state.module);
    WNDCLASSW type{};
    type.lpfnWndProc = window_proc;
    type.hInstance = state.module;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = class_name;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return;
    }
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST), &monitor)) {
        return;
    }
    const auto& work = monitor.rcWork;
    const auto window = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME, class_name, L"The X-Files developer tools",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, work.left + 16, work.top + 16,
        std::min(1200L, work.right - work.left - 32), std::min(850L, work.bottom - work.top - 32),
        nullptr, nullptr, state.module, nullptr);
    if (window) {
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        SetFocus(state.search);
    }
}
}

namespace devtools {
using namespace inspector;

void request_inspector() {
    state.requested = true;
}

void pump_inspector_messages() {
    if (!state.prompting && IsWindowEnabled(state.window)) {
        pump_tool_messages(state.window);
    }
}

void update_inspector(HWND game, bool available) {
    if (!available || state.prompting) {
        show_hotspots(game, false, {});
        return;
    }
    try {
        if (state.requested) {
            state.requested = false;
            open(game);
        }
        if (!state.window || IsIconic(state.window)) {
            show_hotspots(game, false, {});
            return;
        }
        if (state.showing_state && SendMessageW(state.hotspots, BM_GETCHECK, 0, 0)) {
            show_hotspots(game, true, collect_interaction_targets());
        } else {
            show_hotspots(game, false, {});
        }
        if (GetTickCount64() - state.updated < 250) {
            return;
        }
        state.updated = GetTickCount64();
        collect_state_history(state.state_history, state.snapshot);
        if (state.showing_state) {
            if (SendMessageW(state.refresh, BM_GETCHECK, 0, 0) &&
                GetTickCount64() - state.state_updated >= 1000) {
                state.snapshot = inspect_game();
                state.state_updated = GetTickCount64();
                collect_state_history(state.state_history, state.snapshot, true);
                update_game_state_view();
            }
            update_state_history_view();
            return;
        }
        if (state.showing_database) {
            update_database_browser(state.database);
            return;
        }
        if ((SendMessageW(state.captions, BM_GETCHECK, 0, 0) ||
             SendMessageW(state.coverage, CB_GETCURSEL, 0, 0) > 0) &&
            !state.caption_index) {
            start_caption_index();
        }
        if (state.caption_index &&
            state.caption_generation != media::subtitles::install_generation()) {
            state.caption_index.reset();
            state.indexed = 0;
            if (SendMessageW(state.captions, BM_GETCHECK, 0, 0) ||
                SendMessageW(state.coverage, CB_GETCURSEL, 0, 0) > 0) {
                start_caption_index();
            }
            populate();
        }
        const auto indexed = state.caption_index ? state.caption_index->completed() : 0;
        if (indexed != state.indexed) {
            state.indexed = indexed;
            populate();
            if (state.caption_index && !state.dirty) {
                set_text(state.status,
                         L"Checking captions: " + std::to_wstring(indexed) + L" / " +
                             std::to_wstring(state.caption_index->total()) + L" indexed, " +
                             std::to_wstring(state.caption_index->skipped()) + L" unreadable.");
            }
        }
        state.movies = playback::inspect_movies();
        if (!state.browsing) {
            populate();
        }
        describe();
    } catch (...) {
        show_hotspots(game, false, {});
        if (state.window) {
            SetWindowTextW(state.status, L"Inspector data unavailable. Close and reopen to retry.");
        }
    }
}

void release_inspector() {
    release_state_capture();
    if (state.window) {
        DestroyWindow(state.window);
    }
    dispose_resources();
    state = State{};
}
}
