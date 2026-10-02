#pragma once
#include "clip_catalog.h"
#include "clip_notes.h"
#include "preview.h"
#include "artwork.h"
#include "caption_index.h"
#include "game_state.h"
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
    state_snapshot_id
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
void create_controls();
LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
}
