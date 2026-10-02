#include "assets.h"
#include "devtools/clip_catalog.h"
#include <algorithm>
#include <map>
#include <tuple>
#ifdef _WIN32
#include <windows.h>
#endif

namespace devtools {
namespace {
std::wstring asset_key(const std::filesystem::path& path) {
    return lower(path.generic_wstring());
}

struct AssetFormat {
    std::wstring_view extension, type;
    bool previewable = false;
};

const AssetFormat* asset_format(const std::filesystem::path& path) {
    static constexpr AssetFormat localization{L".dll", L"Localization"};
    static constexpr AssetFormat font_loader{L".for", L"Other asset"};
    const auto filename = lower(path.filename().wstring());
    if (filename == L"xfilesc.dll" || filename == L"xfilese.dll" || filename == L"xfiless.dll" ||
        filename == L"xfilest.dll") {
        return &localization;
    }
    if (filename == L"dlg.for" || filename == L"jrn.for" || filename == L"phn.for" ||
        filename == L"hcd.for") {
        return &font_loader;
    }
    static constexpr AssetFormat formats[] = {
        {L".xmv", L"Movie", true}, {L".mov", L"Movie", true},
        {L".mpg", L"Movie"},       {L".mpeg", L"Movie"},
        {L".vob", L"Movie"},       {L".nmv", L"Navigation archive", true},
        {L".dmv", L"Audio", true}, {L".amv", L"Audio", true},
        {L".wav", L"Audio"},       {L".aif", L"Audio"},
        {L".aiff", L"Audio"},      {L".bmp", L"Image"},
        {L".dib", L"Image"},       {L".ico", L"Image"},
        {L".cur", L"Image"},       {L".hdb", L"Database"},
        {L".ttf", L"Font"},        {L".fon", L"Font"},
        {L".pal", L"Palette"},     {L".pff", L"Image archive"},
        {L".pic", L"Other asset"}, {L".hot", L"Hotspot"},
        {L".mus", L"Audio", true}, {L".xtx", L"Text"},
        {L".xt", L"Text"},         {L".gam", L"Database"},
        {L".ttr", L"Font"}};
    const auto extension = lower(path.extension().wstring());
    const auto found = std::find_if(std::begin(formats), std::end(formats), [&](const auto& entry) {
        return entry.extension == extension;
    });
    return found == std::end(formats) ? nullptr : found;
}

std::wstring asset_type(const std::filesystem::path& path) {
    const auto* format = asset_format(path);
    return std::wstring(format ? format->type : L"Other asset");
}

bool supported(const std::filesystem::path& path) {
    return asset_format(path) != nullptr;
}

bool reparse(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    std::error_code error;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, error));
#endif
}

struct ScanEntry {
    std::filesystem::path path;
    bool directory = false, regular = false, linked = false;
};

std::vector<ScanEntry> scan_folder(const std::filesystem::path& directory,
                                   DatabaseAssetCatalog& result, std::size_t limit) {
    std::vector<ScanEntry> files;
    const auto add = [&](ScanEntry entry) {
        if (result.scanned_entries >= limit) {
            result.truncated = true;
            files.clear();
            return false;
        }
        ++result.scanned_entries;
        files.push_back(std::move(entry));
        return true;
    };
#ifdef _WIN32
    WIN32_FIND_DATAW data{};
    const auto handle = FindFirstFileW((directory / L"*").c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        result.truncated = result.truncated || GetLastError() != ERROR_FILE_NOT_FOUND;
        return files;
    }

    struct FindHandle {
        HANDLE value;

        ~FindHandle() {
            FindClose(value);
        }
    } owner{handle};

    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        const auto attributes = data.dwFileAttributes;
        const bool folder = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!add({directory / data.cFileName, folder,
                  !folder && !(attributes & FILE_ATTRIBUTE_DEVICE),
                  (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0})) {
            return files;
        }
    } while (FindNextFileW(handle, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        files.clear();
        result.truncated = true;
    }
#else
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        std::error_code type_error;
        if (!add({it->path(), it->is_directory(type_error), it->is_regular_file(type_error),
                  reparse(it->path())})) {
            return files;
        }
    }
    if (error) {
        files.clear();
        result.truncated = true;
    }
#endif
    return files;
}

bool contained(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto component = path.begin();
    for (const auto& parent : root) {
        if (component == path.end() || asset_key(parent) != asset_key(*component)) {
            return false;
        }
        ++component;
    }
    return component != path.end();
}

bool physical_file(const std::filesystem::path& root, const std::filesystem::path& relative,
                   std::filesystem::path& physical) {
    auto at = root;
    for (const auto& component : relative) {
        at /= component;
        if (reparse(at)) {
            return false;
        }
    }
    std::error_code error;
    physical = std::filesystem::canonical(at, error);
    if (error || !contained(root, physical) || !std::filesystem::is_regular_file(physical, error)) {
        physical.clear();
        return false;
    }
    return true;
}

bool same_label(const game_assets::ClipLabel& a, const game_assets::ClipLabel& b) {
    return std::tie(a.location, a.scene, a.kind, a.category, a.node, a.offset) ==
           std::tie(b.location, b.scene, b.kind, b.category, b.node, b.offset);
}

std::wstring summary(const DatabaseAsset& asset) {
    auto result = asset.path.generic_wstring() + L" / " + asset.type +
                  (asset.present ? L" / Installed" : L" / Referenced, file missing");
    for (const auto& label : asset.labels) {
        result += L" / " + label.location + L" / " + label.scene;
        if (!label.category.empty()) {
            result += L" / " + label.category;
        }
        if (label.node) {
            result += L" / Node " + std::to_wstring(*label.node);
        }
    }
    for (const auto& reference : asset.references) {
        result += L" / VCAssetRef " + std::to_wstring(reference.id) +
                  (reference.state_database ? L" (state)" : L" (HDB)");
    }
    return result;
}
}

std::optional<std::filesystem::path> database_asset_path(const std::filesystem::path& path) {
    auto text = path.generic_wstring();
    if (text.empty() || text.size() > 1024) {
        return std::nullopt;
    }
    for (auto& ch : text) {
        if (ch < 0x20 || ch == 0x7f || ch == L':' || ch == L'<' || ch == L'>' || ch == L'"' ||
            ch == L'|' || ch == L'?' || ch == L'*') {
            return std::nullopt;
        }
        if (ch == L'\\') {
            ch = L'/';
        }
    }
    const std::filesystem::path relative(text);
    if (relative.is_absolute() || relative.has_root_path()) {
        return std::nullopt;
    }
    for (const auto& component : relative) {
        const auto value = component.wstring();
        if (value == L".") {
            continue;
        }
        if (value.empty() || value == L".." || value.back() == L'.' || value.back() == L' ') {
            return std::nullopt;
        }
    }
    const auto normalized = relative.lexically_normal();
    return normalized == L"." ? std::nullopt : std::optional(normalized);
}

std::optional<std::filesystem::path> database_asset_file(const std::filesystem::path& root,
                                                         const std::filesystem::path& path) {
    const auto relative = database_asset_path(path);
    if (root.empty() || !relative) {
        return std::nullopt;
    }
    std::error_code error;
    const auto canonical_root = std::filesystem::canonical(root, error);
    std::filesystem::path physical;
    if (error || !physical_file(canonical_root, *relative, physical)) {
        return std::nullopt;
    }
    return physical;
}

bool database_asset_previewable(const std::filesystem::path& path) {
    const auto* format = asset_format(path);
    return format && format->previewable;
}

DatabaseAssetCatalog database_assets(const std::filesystem::path& root,
                                     std::span<const game_assets::ClipEntry> labels,
                                     const NativeDatabaseSnapshot& snapshot,
                                     DatabaseAssetLimits limits) {
    DatabaseAssetCatalog result;
    std::map<std::wstring, DatabaseAsset> entries;
    std::error_code error;
    const auto canonical_root =
        root.empty() ? std::filesystem::path{} : std::filesystem::canonical(root, error);
    const bool root_ok = !canonical_root.empty() && !error;
    const auto add = [&](const std::filesystem::path& requested,
                         const std::filesystem::path& scanned_file = {}) -> DatabaseAsset* {
        const auto path = database_asset_path(requested);
        if (!path) {
            ++result.rejected_paths;
            return nullptr;
        }
        const auto key = asset_key(*path);
        if (auto found = entries.find(key); found != entries.end()) {
            return &found->second;
        }
        if (entries.size() >= limits.max_assets) {
            result.truncated = true;
            return nullptr;
        }
        DatabaseAsset asset;
        asset.path = *path;
        asset.type = asset_type(*path);
        asset.previewable = database_asset_previewable(*path);
        if (!scanned_file.empty()) {
            // Traversal checked each component. Preview revalidates the path when opened.
            asset.present = true;
            asset.physical_path = scanned_file;
        } else if (root_ok) {
            asset.present = physical_file(canonical_root, *path, asset.physical_path);
        }
        return &entries.emplace(key, std::move(asset)).first->second;
    };
    if (!limits.selected_asset.empty() && database_asset_file(root, limits.selected_asset)) {
        add(limits.selected_asset);
    }
    std::vector<std::pair<std::filesystem::path, unsigned>> folders;
    if (root_ok && limits.max_directories) {
        folders.emplace_back(canonical_root, 0);
    } else if (root_ok) {
        result.truncated = true;
    }
    for (std::size_t folder = 0; folder < folders.size(); ++folder) {
        const auto [directory, depth] = folders[folder];
        auto files = scan_folder(directory, result, limits.max_entries);
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            const auto ak = asset_key(a.path), bk = asset_key(b.path);
            return ak == bk ? a.path < b.path : ak < bk;
        });
        for (const auto& file : files) {
            if (file.linked) {
                ++result.rejected_paths;
                continue;
            }
            if (file.directory) {
                const auto name = lower(file.path.filename().wstring());
                if (name.starts_with(L".") ||
                    (!limits.include_other_files &&
                     (name == L"tools" || name == L"saves" || name == L"docs" || name == L"logs" ||
                      name == L"licenses" || name == L"captions"))) {
                    continue;
                }
                if (depth >= limits.max_depth || folders.size() >= limits.max_directories) {
                    result.truncated = true;
                } else {
                    folders.emplace_back(file.path, depth + 1);
                }
            } else if (file.regular && (limits.include_other_files || supported(file.path))) {
                add(file.path.lexically_relative(canonical_root), file.path);
            }
        }
        if (result.scanned_entries >= limits.max_entries) {
            break;
        }
    }
    std::size_t label_count = 0, reference_count = 0, label_entries = 0, object_entries = 0;
    for (const auto& entry : labels) {
        if (label_count >= limits.max_labels || label_entries++ >= limits.max_labels) {
            result.truncated = true;
            break;
        }
        auto* asset = add(entry.movie);
        for (const auto& label : entry.labels) {
            if (label_count >= limits.max_labels) {
                result.truncated = true;
                break;
            }
            ++label_count;
            if (!asset) {
                continue;
            }
            asset->labels.push_back(label);
            asset->label_offsets.push_back(label.offset);
        }
    }
    for (const auto& object : snapshot.objects) {
        if (object_entries++ >= limits.max_references) {
            result.truncated = true;
            break;
        }
        if (object.class_id != 0x35 || object.description.empty()) {
            continue;
        }
        if (reference_count++ >= limits.max_references) {
            result.truncated = true;
            break;
        }
        if (auto* asset = add(object.description)) {
            asset->references.push_back(object.key());
        }
    }
    for (auto& [key, asset] : entries) {
        std::sort(asset.labels.begin(), asset.labels.end(), [](const auto& a, const auto& b) {
            return std::tie(a.location, a.scene, a.kind, a.category, a.node, a.offset) <
                   std::tie(b.location, b.scene, b.kind, b.category, b.node, b.offset);
        });
        asset.labels.erase(std::unique(asset.labels.begin(), asset.labels.end(), same_label),
                           asset.labels.end());
        std::sort(asset.references.begin(), asset.references.end(),
                  [](const auto& a, const auto& b) {
                      return std::tie(a.state_database, a.class_id, a.id) <
                             std::tie(b.state_database, b.class_id, b.id);
                  });
        asset.references.erase(std::unique(asset.references.begin(), asset.references.end()),
                               asset.references.end());
        std::sort(asset.label_offsets.begin(), asset.label_offsets.end());
        asset.label_offsets.erase(
            std::unique(asset.label_offsets.begin(), asset.label_offsets.end()),
            asset.label_offsets.end());
        asset.summary = summary(asset);
        result.assets.push_back(std::move(asset));
    }
    return result;
}

std::optional<std::size_t> database_asset_find(const DatabaseAssetCatalog& catalog,
                                               const std::filesystem::path& path) {
    const auto relative = database_asset_path(path);
    if (!relative) {
        return std::nullopt;
    }
    const auto key = asset_key(*relative);
    const auto found = std::lower_bound(
        catalog.assets.begin(), catalog.assets.end(), key,
        [](const auto& asset, const auto& value) { return asset_key(asset.path) < value; });
    if (found == catalog.assets.end() || asset_key(found->path) != key) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - catalog.assets.begin());
}

void database_asset_refresh_references(DatabaseAssetCatalog& catalog,
                                       const NativeDatabaseSnapshot& snapshot) {
    for (auto& asset : catalog.assets) {
        asset.references.clear();
    }
    for (const auto& object : snapshot.objects) {
        if (object.class_id == 0x35) {
            if (const auto index = database_asset_find(catalog, object.description)) {
                catalog.assets[*index].references.push_back(object.key());
            }
        }
    }
    for (auto& asset : catalog.assets) {
        std::sort(asset.references.begin(), asset.references.end(),
                  [](const auto& a, const auto& b) {
                      return std::tie(a.state_database, a.class_id, a.id) <
                             std::tie(b.state_database, b.class_id, b.id);
                  });
        asset.references.erase(std::unique(asset.references.begin(), asset.references.end()),
                               asset.references.end());
        asset.summary = summary(asset);
    }
}

game_assets::HotspotFile database_asset_hotspots(const std::filesystem::path& root,
                                                 const DatabaseAsset& asset) {
    if (asset.type == L"Hotspot") {
        if (const auto path = database_asset_file(root, asset.path)) {
            return game_assets::load_hotspots(*path);
        }
    }
    game_assets::HotspotFile result;
    result.status = L"Hotspot file is missing or its path is unsafe";
    return result;
}

std::vector<std::size_t> database_asset_siblings(const DatabaseAssetCatalog& catalog,
                                                 std::size_t index) {
    std::vector<std::size_t> result;
    if (index >= catalog.assets.size()) {
        return result;
    }
    const auto& asset = catalog.assets[index];
    if (asset.type != L"Hotspot" && !asset.previewable && asset.type != L"Movie") {
        return result;
    }
    const auto stem = asset_key(asset.path.parent_path() / asset.path.stem());
    for (std::size_t at = 0; at < catalog.assets.size(); ++at) {
        const auto& candidate = catalog.assets[at];
        if (at != index &&
            (candidate.type == L"Hotspot" || candidate.previewable || candidate.type == L"Movie") &&
            asset_key(candidate.path.parent_path() / candidate.path.stem()) == stem) {
            result.push_back(at);
        }
    }
    return result;
}

}
