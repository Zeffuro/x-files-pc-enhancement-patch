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
#include "subtitle_editor.h"

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

void update_image_reference() {
    if (media::navigation_archive(state.selected)) {
        const auto* live = selected_movie();
        const auto image = state.player ? state.player->image() : live ? live->image : std::nullopt;
        auto hint = state.player ? L"Preview image" : L"Last drawn image";
        std::wstring identity;
        if (image) {
            identity = std::wstring(hint) + L": " + std::to_wstring(image->sample + 1) + L" of " +
                       std::to_wstring(image->count) + L" on track " +
                       std::to_wstring(image->track) + L"\r\n" +
                       media::frame_key(state.selected, *image);
        } else {
            identity = L"Navigation archive. No image has been decoded for this preview.";
        }
        if (GetFocus() != state.hint) {
            set_text(state.hint, identity);
        }
    }
}

void describe() {
    const auto note = state.selected.empty() ? std::nullopt : state.annotations.get(state.selected);
    set_text(state.title,
             note && !note->label.empty() ? note->label : state.selected.generic_wstring());
    std::wstring info = state.selected.empty() ? L"Select a clip to inspect it."
                                               : state.catalog.metadata(state.selected);
    if (const auto* movie = selected_movie()) {
        info += L"\r\nVideo: " + std::wstring(movie->video ? L"yes" : L"no") + L"    Audio: " +
                (movie->audio ? L"yes" : L"no");
        info += L"\r\nDraw area: " + std::to_wstring(movie->width) + L" x " +
                std::to_wstring(movie->height) + L" at " + std::to_wstring(movie->left) + L", " +
                std::to_wstring(movie->top);
        if (!media::navigation_archive(state.selected)) {
            set_text(state.hint,
                     movie->preview.pixels.empty()
                         ? L"No video frame. This may be an audio or unopened clip."
                         : L"Last decoded frame. Other clips can cover it in the game.");
        }
    } else {
        if (!media::navigation_archive(state.selected)) {
            set_text(state.hint, state.selected.empty() ? L"No clip selected."
                                 : state.browsing
                                     ? L"Use Play preview to open this movie independently."
                                     : L"This movie is no longer open.");
        }
    }
    if (!state.selected.empty() &&
        !state.catalog.installed_keys.contains(catalog_key(state.selected))) {
        info += L"\r\nFile not found in local game folders.";
    }
    if (state.player) {
        info += L"\r\nPreview tracks: " +
                std::wstring(state.player->has_video() ? L"video" : L"no video") + L" / " +
                (state.player->has_audio() ? L"audio" : L"no audio");
        if (!media::navigation_archive(state.selected)) {
            set_text(state.hint, state.player->has_video()
                                     ? L"Independent preview. The game keeps running."
                                     : L"Audio-only preview. Use Play preview to listen.");
        }
    }
    update_image_reference();
    DWORD first = 0, last = 0;
    SendMessageW(state.details, EM_GETSEL, reinterpret_cast<WPARAM>(&first),
                 reinterpret_cast<LPARAM>(&last));
    if (text(state.details) != info && GetFocus() != state.details) {
        set_text(state.details, info);
        SendMessageW(state.details, EM_SETSEL, first, last);
    }
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
    update_image_reference();
    EnableWindow(state.stop, state.player != nullptr);
    EnableWindow(state.seek, state.player != nullptr);
}

void show_view() {
    for (auto window :
         {state.coverage,     state.group,         state.place,     state.compact,
          state.search_label, state.search,        state.filenames, state.labels,
          state.notes_search, state.captions,      state.list,      state.title,
          state.preview,      state.clock,         state.play,      state.stop,
          state.subtitle,     state.seek,          state.caption,   state.hint,
          state.details,      state.label_heading, state.label,     state.notes_heading,
          state.notes,        state.save}) {
        ShowWindow(window, state.showing_state ? SW_HIDE : SW_SHOW);
    }
    for (auto window : {state.refresh, state.hotspots, state.state_text}) {
        ShowWindow(window, state.showing_state ? SW_SHOW : SW_HIDE);
    }
    if (state.showing_state) {
        set_text(state.status, L"Read-only. Expand groups to inspect values. Ctrl+C copies the "
                               L"selected row. Clear Live updates to hold values still.");
    }
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
    state = State{};
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
        if (GetTickCount64() - state.updated < 250) {
            return;
        }
        state.updated = GetTickCount64();
        if (SendMessageW(state.hotspots, BM_GETCHECK, 0, 0)) {
            const auto snapshot = inspect_game();
            show_hotspots(game, true, snapshot.targets);
        } else {
            show_hotspots(game, false, {});
        }
        if (state.showing_state) {
            if (SendMessageW(state.refresh, BM_GETCHECK, 0, 0)) {
                state.snapshot = inspect_game();
                update_state_tree(state.state_text, state.snapshot);
            }
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
        if (state.window) {
            SetWindowTextW(state.status, L"Inspector data unavailable. Close and reopen to retry.");
        }
    }
}

void release_inspector() {
    if (state.window) {
        DestroyWindow(state.window);
    }
    dispose_resources();
    state = State{};
}
}
