#include "devtools/database/assets.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Fixture {
    std::filesystem::path root;

    Fixture() {
#ifdef _WIN32
        const auto process = GetCurrentProcessId();
#else
        const auto process = getpid();
#endif
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("xfiles-database-assets-test-" + std::to_string(process) + "-" +
                std::to_string(stamp));
        require(std::filesystem::create_directory(root), "Asset fixture creation failed");
        std::filesystem::create_directories(root / "game" / "XN");
        std::filesystem::create_directories(root / "game" / "XS");
        std::filesystem::create_directories(root / "outside");
        for (const auto* file :
             {"game/XN/7.XMV", "game/XN/7.nmv", "game/XS/9.amv", "game/cover.bmp",
              "game/XFILES.HDB", "game/X.PFF", "game/XFILES.GAM", "outside/escape.xmv",
              "game/XS/10.dmv", "game/XS/11.mus", "game/DLG.TTR"}) {
            std::ofstream(root / file, std::ios::binary).put('\0');
        }
        std::ofstream hot(root / "game/XN/7.HOT", std::ios::binary);
        const char header[] = {'H', 'S', 'P', 'T', 0, 0, 0, 0};
        hot.write(header, sizeof(header));
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

devtools::NativeDatabaseObject reference(unsigned id, std::wstring path, bool state = false) {
    devtools::NativeDatabaseObject object;
    object.class_id = 0x35;
    object.id = id;
    object.description = std::move(path);
    object.state_database = state;
    return object;
}
}

void test_assets() {
    using namespace devtools;
    Fixture fixture;
    {
        Fixture second;
        require(fixture.root != second.root && std::filesystem::exists(fixture.root),
                "Asset fixtures collided");
    }
    const auto root = fixture.root / "game";
    game_assets::ClipLabel label;
    label.location = L"Comity Inn";
    label.scene = L"Hallway";
    label.node = 4;
    label.offset = 100;
    auto second = label;
    second.offset = 200;
    std::vector<game_assets::ClipEntry> labels{{L"xn/7.xmv", {label, label, second}},
                                               {L"XN/7.XMV", {label}},
                                               {L"XV/88.xmv", {label}},
                                               {L"../outside/escape.xmv", {label}}};
    NativeDatabaseSnapshot snapshot;
    snapshot.objects = {reference(7, L"XN\\7.xmv"),
                        reference(7, L"xn/7.XMV"),
                        reference(7, L"XN/7.xmv", true),
                        reference(9, L"XS/9.amv"),
                        reference(88, L"XV/88.xmv"),
                        reference(90, L"./XV/90.xmv"),
                        reference(91, L"XN/7.nmv"),
                        reference(92, L"../outside/escape.xmv"),
                        reference(93, L"C:/outside/escape.xmv"),
                        reference(94, L"XN/7.xmv:secret"),
                        reference(95, L"//host/share/a.xmv")};
    auto catalog = database_assets(root, labels, snapshot);
    require(!catalog.truncated && catalog.rejected_paths == 5, "Invalid paths not rejected");
    require(catalog.assets.size() == 13, "Installed and missing asset coverage failed");
    for (const auto* path : {L"XS/10.dmv", L"XS/11.mus"}) {
        const auto audio = database_asset_find(catalog, path);
        require(audio && catalog.assets[*audio].type == L"Audio" &&
                    catalog.assets[*audio].previewable,
                "QuickTime audio classification failed");
    }
    for (const auto& entry :
         {std::pair{L"X.PFF", L"Image archive"}, std::pair{L"XFILES.GAM", L"Database"},
          std::pair{L"DLG.TTR", L"Font"}}) {
        const auto file = database_asset_find(catalog, entry.first);
        require(file && catalog.assets[*file].type == entry.second &&
                    !catalog.assets[*file].previewable,
                "Stored asset classification failed");
    }
    const auto movie = database_asset_find(catalog, L"xn\\7.XMV");
    require(movie.has_value(), "Case or separator identity failed");
    const auto& asset = catalog.assets[*movie];
    require(asset.present && asset.previewable && !asset.physical_path.empty(),
            "Installed movie presence failed");
    require(asset.references.size() == 2 && asset.labels.size() == 2 &&
                asset.label_offsets == std::vector<std::uint64_t>{100, 200},
            "Duplicate references or labels were not merged");
    require(asset.summary.find(L"Comity Inn") != std::wstring::npos &&
                asset.summary.find(L"Node 4") != std::wstring::npos &&
                asset.summary.find(L"VCAssetRef 7") != std::wstring::npos,
            "Friendly searchable summary missing labels or cached IDs");
    const auto archive = database_asset_find(catalog, L"XN/7.nmv");
    const auto hot = database_asset_find(catalog, L"xn/7.hot");
    require(hot && catalog.assets[*hot].type == L"Hotspot" && !catalog.assets[*hot].previewable &&
                database_asset_hotspots(root, catalog.assets[*hot]).valid,
            "Hotspot asset classification or safe loading failed");
    const auto siblings = database_asset_siblings(catalog, *hot);
    require(siblings.size() == 2 &&
                std::find(siblings.begin(), siblings.end(), *movie) != siblings.end() &&
                std::find(siblings.begin(), siblings.end(), *archive) != siblings.end(),
            "Same-stem media siblings failed");
    require(database_asset_siblings(catalog, catalog.assets.size()).empty(),
            "Invalid sibling index accepted");
    require(!database_asset_hotspots({}, catalog.assets[*hot]).valid &&
                !database_asset_hotspots(root, catalog.assets[*movie]).valid,
            "Unsafe hotspot read accepted");
    require(archive && archive != movie && catalog.assets[*archive].references.size() == 1,
            "Different extensions falsely joined");
    require(!database_asset_find(catalog, L"XN/7") &&
                !database_asset_find(catalog, L"../outside/escape.xmv"),
            "Extensionless or traversal search falsely resolved");
    const auto missing = database_asset_find(catalog, L"XV/88.xmv");
    require(missing && !catalog.assets[*missing].present &&
                catalog.assets[*missing].physical_path.empty() &&
                catalog.assets[*missing].references.size() == 1 &&
                catalog.assets[*missing].labels.size() == 1,
            "Referenced missing row lost metadata");
    const auto bitmap = database_asset_find(catalog, L"cover.bmp");
    require(database_asset_find(catalog, L"X.PFF") && database_asset_find(catalog, L"XFILES.GAM"),
            "Original non-preview asset formats omitted");
    require(bitmap && catalog.assets[*bitmap].present && !catalog.assets[*bitmap].previewable,
            "Unsupported preview was advertised");
    require(database_asset_path(L"./XN/7.xmv") == std::filesystem::path(L"XN/7.xmv"),
            "Safe dot path was not normalized");
    for (const auto* path : {L".", L"./", L"../x.xmv", L"XN/../x.xmv", L"/x.xmv", L"C:foo.xmv",
                             L"\\\\host\\x.xmv", L"XN/7.xmv.", L"XN/7.xmv ", L"XN/a?b.xmv"}) {
        require(!database_asset_path(path), "Unsafe path accepted");
    }
    require(database_asset_file(root, L"XN/7.XMV").has_value() &&
                !database_asset_file(root, L"../outside/escape.xmv") &&
                !database_asset_file(root, L"XV/88.xmv") && !database_asset_file({}, L"XN/7.XMV"),
            "Physical preview containment failed");
    const auto again = database_assets(root, labels, snapshot);
    require(again.assets.size() == catalog.assets.size(), "Repeat catalog changed size");
    for (std::size_t i = 0; i < catalog.assets.size(); ++i) {
        require(catalog.assets[i].path == again.assets[i].path &&
                    catalog.assets[i].summary == again.assets[i].summary,
                "Catalog ordering was not deterministic");
    }
    auto updated = catalog;
    NativeDatabaseSnapshot refreshed;
    refreshed.objects = {reference(71, L"XN/7.nmv"), reference(71, L"XN/7.nmv")};
    database_asset_refresh_references(updated, refreshed);
    require(updated.assets[*movie].references.empty() &&
                updated.assets[*movie].summary.find(L"VCAssetRef") == std::wstring::npos &&
                updated.assets[*archive].references.size() == 1 &&
                updated.assets[*archive].references[0].id == 71,
            "Cached asset references stayed stale after refresh");
    DatabaseAssetLimits limits;
    limits.max_entries = 2;
    auto bounded = database_assets(root, {}, {}, limits);
    require(bounded.truncated && bounded.scanned_entries == 2 && bounded.assets.empty(),
            "Entry cap selected an unstable partial directory");
    limits = {};
    limits.max_assets = 2;
    bounded = database_assets(root, labels, snapshot, limits);
    require(bounded.truncated && bounded.assets.size() == 2, "Asset count cap failed");
    limits = {};
    limits.max_labels = 1;
    limits.max_references = 1;
    bounded = database_assets(root, labels, snapshot, limits);
    const auto bounded_movie = database_asset_find(bounded, L"XN/7.xmv");
    require(bounded.truncated && bounded_movie &&
                bounded.assets[*bounded_movie].labels.size() == 1 &&
                bounded.assets[*bounded_movie].references.size() == 1,
            "Metadata limits failed");
    std::error_code link_error;
    std::filesystem::create_directory_symlink(fixture.root / "outside", root / "linked",
                                              link_error);
    std::error_code status_error;
    const auto link_status = std::filesystem::symlink_status(root / "linked", status_error);
    if (!link_error && !status_error && std::filesystem::is_symlink(link_status)) {
        snapshot.objects = {reference(99, L"linked/escape.xmv")};
        catalog = database_assets(root, {}, snapshot);
        const auto linked = database_asset_find(catalog, L"linked/escape.xmv");
        require(linked && !catalog.assets[*linked].present && catalog.rejected_paths == 1 &&
                    !database_asset_file(root, L"linked/escape.xmv"),
                "Reparse path escaped the game root");
    } else {
        std::cout << "Directory symlink fixture unavailable: "
                  << (link_error     ? link_error.message()
                      : status_error ? status_error.message()
                                     : "Created path is not a symbolic link")
                  << '\n';
    }
    std::filesystem::create_directory(root / "tools");
    std::ofstream(root / "tools/unknown.bin", std::ios::binary) << "raw";
    std::ofstream(root / "notes.XTX", std::ios::binary) << "text";
    for (const auto* file :
         {"XFILESC.DLL", "XFILESE.DLL", "XFILESS.DLL", "XFILEST.DLL", "ddraw.dll", "XFilesMpeg.dll",
          "avcodec-63.dll", "readme.md", "options.ini"}) {
        std::ofstream(root / file, std::ios::binary) << "fixture";
    }
    for (const auto* folder : {"docs", "saves", "logs", "licenses"}) {
        std::filesystem::create_directory(root / folder);
        std::ofstream(root / folder / "misleading.XTX", std::ios::binary) << "fixture";
    }
    const auto game = database_assets(root, {}, {});
    DatabaseAssetLimits direct_limits;
    direct_limits.selected_asset = L"tools/unknown.bin";
    direct_limits.max_assets = 1;
    const auto direct = database_assets(root, {}, {}, direct_limits);
    require(direct.truncated && direct.assets.size() == 1 &&
                database_asset_find(direct, L"tools/unknown.bin"),
            "Catalog bound discarded explicitly opened file");
    for (const auto* file : {L"XFILESC.DLL", L"XFILESE.DLL", L"XFILESS.DLL", L"XFILEST.DLL"}) {
        const auto localization = database_asset_find(game, file);
        require(localization && game.assets[*localization].type == L"Localization",
                "Game localization DLL omitted");
    }
    for (const auto* file :
         {L"ddraw.dll", L"XFilesMpeg.dll", L"avcodec-63.dll", L"readme.md", L"options.ini",
          L"tools/unknown.bin", L"docs/misleading.XTX", L"saves/misleading.XTX",
          L"logs/misleading.XTX", L"licenses/misleading.XTX"}) {
        require(!database_asset_find(game, file), "Non-game file included in asset scan");
    }
    DatabaseAssetLimits all_files;
    all_files.include_other_files = true;
    const auto all = database_assets(root, {}, {}, all_files);
    const auto unknown = database_asset_find(all, L"tools/unknown.bin");
    const auto xt = database_asset_find(all, L"notes.XTX");
    require(unknown && all.assets[*unknown].present && !all.assets[*unknown].previewable &&
                all.assets[*unknown].type == L"Other asset" && xt &&
                all.assets[*xt].type == L"Text",
            "All-file browsing or XT classification failed");
    catalog = database_assets({}, labels, snapshot);
    require(!catalog.assets.empty(), "Missing game root lost referenced assets");
    for (const auto& row : catalog.assets) {
        require(!row.present && row.physical_path.empty(), "Empty root accessed current directory");
    }
}

int main() {
    try {
        test_assets();
    } catch (const std::exception& error) {
        std::cerr << "Asset catalog test failed: " << error.what() << '\n';
        return 1;
    }
}
