#include "setup/catalog.h"
#include "setup/media.h"
#include "launcher/session.h"
#include "dispatch.h"

#include <windows.h>
#include <fstream>
#include <iostream>

void verify_disc_import(const std::filesystem::path& base);

int wmain(int argc, wchar_t** argv) {
    try {
        namespace fs = std::filesystem;
        using test::require;
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
        const auto base = fs::temp_directory_path() /
                          (L"xfiles-setup-test-" + std::to_wstring(GetCurrentProcessId()));
        fs::create_directories(base / L"source");
        verify_disc_import(base);
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
            const auto source = inspect_media(argv[1]);
            const auto destination = fs::absolute(argv[2]);
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
            install_media(source, fs::absolute(argv[2]), true, [&](unsigned percent) {
                if (percent / 10 != last / 10) {
                    std::cout << percent << "%\n" << std::flush;
                    last = percent;
                }
                return true;
            });
            const auto session = read_session(fs::absolute(argv[2]));
            require(session.media == fs::canonical(argv[2]),
                    "Installed game still references source");
            require(!fs::exists(fs::path(argv[2]) / L"installing.txt"),
                    "Incomplete marker retained");
            for (const auto* notice :
                 {L"LICENSE", L"THIRD_PARTY.md", L"cnc-ddraw.LICENSE", L"FFmpeg.LICENSE",
                  L"zlib.LICENSE", L"README.md", L"docs/controls.md", L"docs/building.md",
                  L"defaults/ddraw.ini", L"defaults/patch.ini"}) {
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
