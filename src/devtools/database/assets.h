#pragma once
#include "native.h"
#include "game/assets/clip_index.h"
#include "game/assets/hotspots.h"
#include <filesystem>
#include <span>

namespace devtools {
struct DatabaseAsset {
    std::filesystem::path path, physical_path;
    std::wstring type, summary;
    std::vector<game_assets::ClipLabel> labels;
    std::vector<DatabaseObjectKey> references;
    std::vector<std::uint64_t> label_offsets;
    bool present = false, previewable = false;
};

struct DatabaseAssetLimits {
    std::size_t max_assets = 30000, max_entries = 60000, max_directories = 512;
    std::size_t max_labels = 30000, max_references = 30000;
    unsigned max_depth = 8;
    bool include_other_files = false;
    std::filesystem::path selected_asset;
};

struct DatabaseAssetCatalog {
    std::vector<DatabaseAsset> assets;
    std::size_t scanned_entries = 0, rejected_paths = 0;
    bool truncated = false;
};

std::optional<std::filesystem::path> database_asset_path(const std::filesystem::path& path);
std::optional<std::filesystem::path> database_asset_file(const std::filesystem::path& root,
                                                         const std::filesystem::path& path);
bool database_asset_previewable(const std::filesystem::path& path);
DatabaseAssetCatalog database_assets(const std::filesystem::path& root,
                                     std::span<const game_assets::ClipEntry> labels,
                                     const NativeDatabaseSnapshot& snapshot,
                                     DatabaseAssetLimits limits = {});
void database_asset_refresh_references(DatabaseAssetCatalog& catalog,
                                       const NativeDatabaseSnapshot& snapshot);
std::optional<std::size_t> database_asset_find(const DatabaseAssetCatalog& catalog,
                                               const std::filesystem::path& path);
game_assets::HotspotFile database_asset_hotspots(const std::filesystem::path& root,
                                                 const DatabaseAsset& asset);
std::vector<std::size_t> database_asset_siblings(const DatabaseAssetCatalog& catalog,
                                                 std::size_t index);
}
