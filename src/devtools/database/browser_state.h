#pragma once
#include "browser.h"
#include "assets.h"
#include "model.h"
#include "font_preview.h"
#include "game/assets/clip_index.h"
#include "game/assets/resource_strings.h"
#include "game/database/database.h"
#include "game/database/io.h"
#include "game/database/offline_index.h"
#include <commctrl.h>
#include <array>
#include <memory>
#include <map>

namespace devtools::database_browser {
enum {
    mode_id = 3100,
    search_id,
    refresh_id,
    jump_id,
    offset_id,
    list_id,
    detail_id,
    status_id,
    relations_id,
    follow_id,
    back_id,
    filter_id,
    tabs_id,
    properties_id,
    title_id,
    preview_id,
    auto_refresh_id,
    content_id,
    font_id,
    strings_search_id,
    strings_list_id,
    strings_text_id,
    strings_copy_id,
    fields_list_id,
    fields_text_id,
    fields_copy_id,
    database_info_id,
    database_classes_id,
    database_browse_id,
    asset_browse_id,
    database_heading_id,
    list_context_id,
    database_view_id
};

enum class Pane { overview, links, raw, text, font, strings, fields };

struct Row {
    std::uint64_t key = 0;
    std::array<std::wstring, 4> columns;
    std::optional<std::size_t> object, asset;
    std::optional<std::uint64_t> offset;
    std::wstring search;
    std::optional<std::size_t> stored;
};

struct Destination {
    std::optional<DatabaseObjectKey> object;
    std::filesystem::path asset;
    bool stored = false;
};

struct BrowserView {
    unsigned mode = 0, filter = 0;
    std::uint32_t class_id = 0;
    std::wstring query;
    std::optional<std::uint64_t> selection;
    std::optional<std::filesystem::path> asset;
    std::wstring asset_type;
    std::optional<std::size_t> offset;
    Pane pane = Pane::overview;
    Pane database_pane = Pane::fields;
    Pane asset_pane = Pane::overview;
    int sort_column = 0, top = 0;
    bool sort_descending = false, valid = false;
};

struct Link {
    std::array<std::wstring, 4> columns;
    Destination destination;
};

struct Browser {
    HWND window = nullptr, mode = nullptr, search = nullptr, refresh = nullptr;
    HWND offset = nullptr, jump = nullptr, list = nullptr, detail = nullptr, status = nullptr;
    HWND relations = nullptr, follow = nullptr, back = nullptr, filter = nullptr;
    HWND tabs = nullptr, properties = nullptr, title = nullptr, preview = nullptr,
         auto_refresh = nullptr, content = nullptr, font_view = nullptr;
    std::unique_ptr<FontPreview> font_preview;
    HWND strings_search = nullptr, strings_list = nullptr, strings_text = nullptr,
         strings_copy = nullptr;
    game_assets::ResourceStrings resource_strings;
    std::optional<std::filesystem::path> resource_strings_path;
    std::vector<std::array<std::wstring, 4>> string_rows;
    std::vector<std::size_t> string_indices;
    bool rebuilding_strings = false;
    HWND fields_list = nullptr, fields_text = nullptr, fields_copy = nullptr;
    HWND database_info = nullptr, database_classes = nullptr, database_browse = nullptr;
    HWND asset_browse = nullptr, database_heading = nullptr, list_context = nullptr,
         database_view = nullptr;
    std::vector<std::array<std::wstring, 4>> field_rows, class_rows;
    std::vector<std::uint32_t> class_ids;
    HFONT fixed_font = nullptr;
    std::filesystem::path root, path, requested_path, requested_asset;
    std::optional<game_assets::Database> database;
    std::optional<game_assets::ClipIndex> clips;
    DatabaseAssetCatalog assets;
    game_assets::OfflineDatabaseIndex stored;
    game_assets::DatabaseIoSnapshot io;
    NativeDatabaseSnapshot native, previous;
    std::vector<DatabaseObjectKey> changes;
    std::vector<BrowserView> history;
    std::array<BrowserView, 8> views;
    std::map<std::wstring, Pane> asset_panes;
    std::vector<Link> links;
    std::vector<Row> rows;
    std::function<void(const std::filesystem::path&)> open_asset;
    std::function<NativeDatabaseSnapshot()> snapshot_provider;
    std::shared_ptr<HWND> archive_window;
    std::optional<std::uint64_t> pending_selection;
    std::optional<std::filesystem::path> pending_asset;
    unsigned mode_index = 0, filter_index = 0;
    Pane database_pane = Pane::fields;
    bool rebuilding = false;
    std::optional<std::size_t> manual_offset;
    std::optional<std::pair<unsigned, std::uint64_t>> described;
    int sort_column = 0;
    bool sort_descending = false;
    ULONGLONG updated = 0, native_updated = 0;
};

std::wstring text(HWND window);
std::wstring lower(std::wstring value);
std::wstring hex(std::uint64_t value);
bool native_mode(const Browser& state);
void select_mode(Browser& state);
std::uint64_t row_key(DatabaseObjectKey key);
const Row* selected_row(const Browser& state);
void describe(Browser& state);
void asset_content(Browser& state, const DatabaseAsset& asset, std::wstring& raw);
void text_candidate_content(Browser& state, const Row& row);
void create_string_controls(Browser& state, HMODULE module, HFONT font);
void filter_strings(Browser& state);
void select_string(Browser& state);
void copy_string(Browser& state);
void load_strings(Browser& state, const std::filesystem::path& path);
void layout_strings(Browser& state, int x, int y, int width, int height, bool visible);
bool string_notification(Browser& state, NMHDR* notification);
void layout(Browser& state);
Pane selected_pane(const Browser& state);
void select_pane(Browser& state, Pane pane);
void configure_tabs(Browser& state);
void create_table_controls(Browser& state, HMODULE module, HFONT font);
void layout_tables(Browser& state, int x, int y, int width, int height, Pane pane);
void update_field_table(Browser& state);
void update_database_table(Browser& state);
void sync_database_class(Browser& state);
bool table_notification(Browser& state, NMHDR* notification);
void copy_field_table(Browser& state);
void browse_database_class(Browser& state);
std::uint32_t filter_class(const Browser& state);
void create_controls(Browser& state, HMODULE module, HFONT font, HWND parent);
LRESULT CALLBACK control_keys(HWND window, UINT message, WPARAM key, LPARAM data, UINT_PTR,
                              DWORD_PTR owner);
void populate(Browser& state);
void configure_filter(Browser& state);
void navigate(Browser& state, const Destination& destination, bool remember);
BrowserView current_view(const Browser& state);
void restore_view(Browser& state, const BrowserView& view);
void switch_view(Browser& state, unsigned mode);
void update_navigation(Browser& state);
void follow(Browser& state);
void copy_property(Browser& state);
}
