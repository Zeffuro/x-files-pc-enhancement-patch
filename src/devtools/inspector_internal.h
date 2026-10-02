#pragma once
#include "clip_catalog.h"
#include "clip_notes.h"
#include "preview.h"
#include "artwork.h"
#include "caption_index.h"
#include "game_state.h"
#include "state_variables.h"
#include "state_history.h"
#include "inspector_state.h"
#include "playback/inspection.h"
#include "inspector_preview_details.h"
#include <commctrl.h>
#include <array>

namespace devtools::inspector {
inline constexpr wchar_t class_name[] = L"XFilesClipInspector";

enum : int {
    live_id = 2001,
    library_id,
    search_id,
    list_id,
    label_id,
    notes_id,
    save_id,
    play_id,
    stop_id,
    seek_id,
    subtitle_id,
    filename_id,
    labels_id,
    notes_search_id,
    captions_id,
    game_state_id,
    refresh_id,
    hotspots_id,
    compact_id,
    group_id,
    place_id,
    coverage_id,
    database_id,
    state_search_id,
    state_snapshot_id,
    state_filter_id,
    state_nonzero_id,
    state_changed_id,
    state_baseline_id,
    state_edit_id,
    state_value_id,
    state_apply_id,
    state_variables_id,
    state_watch_id,
    state_watched_id,
    state_capture_id,
    state_history_filter_id,
    state_history_clear_id,
    state_history_copy_id
};

struct Row {
    std::filesystem::path path;
    std::uint64_t movie = 0;
};

struct State {
    HMODULE module = nullptr;
    HWND window = nullptr, game = nullptr, live = nullptr, library = nullptr, search = nullptr;
    HWND list = nullptr, title = nullptr, preview = nullptr, details = nullptr;
    HWND label_heading = nullptr, label = nullptr, notes_heading = nullptr, notes = nullptr;
    HWND save = nullptr, status = nullptr, search_label = nullptr;
    PreviewDetailsTree preview_details;
    HWND clock = nullptr, caption = nullptr, play = nullptr, stop = nullptr, seek = nullptr,
         subtitle = nullptr;
    HWND filenames = nullptr, labels = nullptr, notes_search = nullptr, captions = nullptr;
    HWND game_state = nullptr, refresh = nullptr, hotspots = nullptr, state_text = nullptr;
    HWND state_search = nullptr, state_snapshot = nullptr;
    HWND state_filter = nullptr, state_nonzero = nullptr, state_changed = nullptr,
         state_baseline = nullptr, state_edit = nullptr, state_value = nullptr,
         state_apply = nullptr, state_variables = nullptr, state_detail = nullptr;
    HWND state_watch = nullptr, state_watched = nullptr, state_capture = nullptr;
    HWND state_history_filter = nullptr, state_history_clear = nullptr,
         state_history_copy = nullptr, state_history_list = nullptr, state_history_status = nullptr;
    std::optional<std::uint64_t> history_selected_sequence;
    StateHistory state_history;
    std::uint64_t history_revision = UINT64_MAX;
    int history_filter = -1;
    bool history_capture = false;
    std::wstring history_coverage;
    std::optional<DatabaseObjectKey> history_selected;
    std::vector<DatabaseObjectKey> history_watches;
    std::vector<std::array<std::wstring, 6>> history_cells;
    std::vector<std::size_t> history_rows;
    StateVariableView state_variable_view;
    StateVariableEdit state_variable_edit;
    ULONGLONG state_updated = 0;
    HWND compact = nullptr, group = nullptr, place = nullptr, coverage = nullptr;
    bool compact_view = true;
    std::unique_ptr<ArtworkCache> artwork;
    std::array<std::shared_ptr<const Artwork>, 64> shown_artwork;
    bool showing_state = false;
    bool showing_database = false;
    HWND database_button = nullptr, database = nullptr;
    std::unique_ptr<Preview> player;
    std::unique_ptr<CaptionIndex> caption_index;
    unsigned indexed = 0;
    std::uint64_t caption_generation = 0;
    GameSnapshot snapshot;
    HFONT font = nullptr, heading = nullptr;
    HIMAGELIST images = nullptr;
    bool requested = false, browsing = false, rebuilding = false, dirty = false, notes_ok = false;
    bool prompting = false;
    ULONGLONG updated = 0;
    ULONGLONG artwork_updated = 0;
    ClipCatalog catalog;
    ClipNotes annotations;
    std::vector<playback::MovieSnapshot> movies;
    std::vector<Row> rows;
    std::filesystem::path selected;
    std::uint64_t selected_movie = 0;
};

extern thread_local State state;

std::wstring text(HWND control);
void set_text(HWND control, const std::wstring& value);
const playback::MovieSnapshot* selected_movie();
std::wstring activity(const playback::MovieSnapshot& movie);
void paint_frame(HDC dc, RECT bounds, const media::Frame* frame, const std::wstring& caption);
void describe();
void update_preview_details();
void update_game_state_view();
void update_state_selection();
void populate();
void update_artwork();
void set_list_density();
void update_save();
bool save_notes();
bool leave_notes();
void select(const Row& row);
void open_database_asset(const std::filesystem::path& path);
void tick_preview();
void start_caption_index();
void layout();
HWND child(const wchar_t* type, const wchar_t* caption, DWORD style = 0, int id = 0);
void create_controls();
LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
}
