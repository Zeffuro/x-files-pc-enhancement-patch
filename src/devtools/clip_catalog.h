#pragma once
#include "game/assets/clip_index.h"
#include "clip_defaults.h"
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace devtools {
std::wstring lower(std::wstring text);
std::wstring catalog_key(const std::filesystem::path& path);

struct ClipCatalog {
    std::optional<game_assets::ClipIndex> index;
    ClipDefaults defaults;
    std::vector<std::filesystem::path> paths;
    std::set<std::wstring> installed_keys;
    std::filesystem::path root, notes_path;
    std::wstring status;
    void load();
    std::set<std::wstring> places(const std::filesystem::path& path) const;
    std::set<std::wstring> places() const;
    std::wstring metadata(const std::filesystem::path& path, bool comments = true) const;
    std::wstring comment(const std::filesystem::path& path) const;
};
}
