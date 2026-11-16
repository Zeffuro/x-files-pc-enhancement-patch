#include "setup/catalog.h"
#include "setup/media.h"
#include "launcher/session.h"
#include "dispatch.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>

void verify_disc_import(const std::filesystem::path& base);
void verify_french_dvd_sources(const std::filesystem::path& image,
                               const std::filesystem::path& folder,
                               const std::filesystem::path& french_cd);

int wmain(int argc, wchar_t** argv) {
    try {
        namespace fs = std::filesystem;
        using test::require;
        if (argc == 5 && std::wstring_view(argv[1]) == L"--french") {
            verify_french_dvd_sources(argv[2], argv[3], argv[4]);
            return 0;
        }
        const std::string hash(64, 'a');
        require(parse_catalog("xv/movie.xmv\t123\t" + hash + "\n").size() == 1,
                "Valid catalog rejected");
        for (const auto& row : {"../outside\t1\t" + hash, "C:/outside\t1\t" + hash,
                                "/outside\t1\t" + hash, std::string("valid\t1\tbad"), std::string{},
                                "same\t1\t" + hash + "\nsame\t1\t" + hash}) {
            bool rejected = false;
            try {
                parse_catalog(row);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Invalid or duplicate catalog path accepted");
        }
        require(media_catalog(false).size() > 3000 && media_catalog(true).size() > 3000,
                "Packaged catalogs incomplete");
        const auto dvd_catalog = media_catalog(true);
        require(dvd_catalog.contains(L"vob/ddigital1.vob") &&
                    dvd_catalog.contains(L"vob/teaser.vob") &&
                    dvd_catalog.contains(L"vob/23412.vob") && !dvd_catalog.contains(L"dlmpg.ini") &&
                    !dvd_catalog.contains(L"dlmpgmci.dll") &&
                    !media_catalog(false).contains(L"vob/teaser.vob"),
                "DVD VOB catalog or native fallback selection is wrong");
        const auto italian = media_catalog(MediaSetId::cd_it);
        const auto japanese = media_catalog(MediaSetId::cd_jp);
        require(italian.size() == 3038 && japanese.size() == 3041,
                "Italian or Japanese catalog lost original disc files");
        require(italian.at(L"nav7.nmv").size == japanese.at(L"nav7.nmv").size &&
                    italian.at(L"nav2.nmv").sha256 != japanese.at(L"nav2.nmv").sha256,
                "Italian or Japanese navigation archive mapping is wrong");
        std::map<std::wstring, std::set<std::string>> identities;
        for (const auto& set : media_sets()) {
            const auto catalog = media_catalog(set.id);
            require(catalog.size() >= 3031, "Language catalog incomplete");
            identities.clear();
            for (const auto& [name, file] : catalog) {
                identities[name].insert(file.sha256);
            }
            require(identify_media_set(identities) == set.id,
                    "Executable identity hid the language resource identity");
            identities.erase(L"xfilest.dll");
            bool rejected = false;
            try {
                identify_media_set(identities);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Missing language resources accepted");
        }
        identities.clear();
        for (const auto id : {MediaSetId::cd_de, MediaSetId::cd_es}) {
            for (const auto& [name, file] : media_catalog(id)) {
                identities[name].insert(file.sha256);
            }
        }
        bool ambiguous = false;
        try {
            identify_media_set(identities);
        } catch (const std::exception&) {
            ambiguous = true;
        }
        require(ambiguous, "Two languages sharing one executable were silently merged");
        const auto base = fs::temp_directory_path() /
                          (L"xfiles-setup-test-" + std::to_wstring(GetCurrentProcessId()));
        fs::create_directories(base / L"source");
        verify_disc_import(base);
        bool supplemental_required = false;
        try {
            inspect_media(base, {true, {}});
        } catch (const std::exception& error) {
            supplemental_required =
                std::string_view(error.what()).find("French CD disc 1") != std::string_view::npos;
        }
        require(supplemental_required, "French DVD missing-supplement error is not actionable");
        std::ofstream(base / L"source" / L"keep.txt") << "keep";
        MediaSource media;
        media.root = fs::canonical(base / L"source");
        validate_destination(media, base / L"new-game");
        for (const auto& destination : {base / L"source", base / L"source" / L"nested"}) {
            bool rejected = false;
            try {
                validate_destination(media, destination);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Existing or nested destination accepted");
        }
        require(fs::is_regular_file(base / L"source" / L"keep.txt"), "Validation changed files");
        fs::create_directory(base / L"portable");
        std::ofstream(base / L"portable" / L"preferences.ini") << "[Game]\n";
        save_session(base / L"portable", {base / L"portable", true});
        fs::rename(base / L"portable", base / L"moved");
        require(read_session(base / L"moved").media == fs::canonical(base / L"moved"),
                "Self-contained installation retained its previous location");
        fs::remove(base / L"moved" / L"preferences.ini");
        fs::remove(base / L"moved" / L"session.ini");
        fs::remove(base / L"moved");
        fs::remove(base / L"source" / L"keep.txt");
        fs::remove(base / L"source");
        fs::remove(base);
        if (argc >= 3) {
            const bool french = argc == 5 && std::wstring_view(argv[1]) == L"--french-install";
            const auto source =
                french ? inspect_media(argv[2], {true, argv[3]}) : inspect_media(argv[1]);
            if (source.set == MediaSetId::dvd_en) {
                const auto has_file = [&](const std::filesystem::path& name) {
                    return std::ranges::any_of(
                        source.files, [&](const MediaFile& file) { return file.relative == name; });
                };
                require(has_file(L"vob/teaser.vob") && has_file(L"vob/23412.vob") &&
                            !has_file(L"dlmpg.ini") && !has_file(L"dlmpgmci.dll"),
                        "DVD import did not preserve VOB assets and native fallback");
            }
            const auto destination = fs::absolute(argv[french ? 4 : 2]);
            const bool parent_existed = fs::is_directory(destination.parent_path());
            const auto cancelled = fs::path(destination.wstring() + L"-cancelled");
            bool stopped = false;
            try {
                install_media(source, cancelled, true, [](unsigned) { return false; });
            } catch (const std::exception&) {
                stopped = true;
            }
            require(stopped && !fs::exists(cancelled), "Cancelled install was published");
            auto damaged = source;
            damaged.files.front().checksum = std::string(64, '0');
            stopped = false;
            const auto bad = fs::path(destination.wstring() + L"-damaged");
            try {
                install_media(damaged, bad, true, [](unsigned) { return true; });
            } catch (const std::exception&) {
                stopped = true;
            }
            require(stopped && !fs::exists(bad), "Failed checksum install was published");
            const auto prefix = L".xfiles-install-" + std::to_wstring(GetCurrentProcessId()) + L"-";
            if (parent_existed) {
                require(fs::is_directory(destination.parent_path()),
                        "Failed install removed an existing parent folder");
                for (const auto& entry : fs::directory_iterator(destination.parent_path())) {
                    require(!entry.path().filename().wstring().starts_with(prefix),
                            "Failed install left temporary files");
                }
            } else {
                require(!fs::exists(destination.parent_path()),
                        "Failed install left new parent folders");
            }
            if (argc == 4) {
                return 0;
            }
            unsigned last = 101;
            install_media(source, destination, true, [&](unsigned percent) {
                if (percent / 10 != last / 10) {
                    std::cout << percent << "%\n" << std::flush;
                    last = percent;
                }
                return true;
            });
            const auto session = read_session(destination);
            require(session.media == fs::canonical(destination),
                    "Installed game still references source");
            require(!fs::exists(destination / L"installing.txt"), "Incomplete marker retained");
            for (const auto* notice :
                 {L"LICENSE", L"THIRD_PARTY.md", L"cnc-ddraw.LICENSE", L"FFmpeg.LICENSE",
                  L"zlib.LICENSE", L"README.md", L"docs/controls.md", L"docs/building.md",
                  L"docs/linux.md", L"defaults/ddraw.ini", L"defaults/patch.ini"}) {
                require(fs::is_regular_file(destination / notice) &&
                            fs::file_size(destination / notice) != 0,
                        "Installed game is missing a notice, guide or default settings");
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
