#include "launcher/staging.h"
#include "ffmpeg_files.h"

#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Fixture {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (L"xfiles-codec-staging-" + std::to_wstring(GetCurrentProcessId()));

    Fixture() {
        require(std::filesystem::create_directory(directory), "Cannot create staging fixture");
    }

    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};

}

int wmain(int argc, wchar_t** argv) {
    try {
        namespace fs = std::filesystem;
        const auto package = fs::absolute(argv[0]).parent_path();
        bool demuxer = false;
        for (const auto* name : ffmpeg_files) {
            const auto path = package / name;
            if (!fs::is_regular_file(path) || fs::file_size(path) == 0) {
                throw std::runtime_error("Missing staged codec file: " + path.string());
            }
            demuxer |= path.filename().wstring().starts_with(L"avformat-");
        }
        if (!demuxer) {
            throw std::runtime_error("Codec staging manifest omits avformat");
        }
        Fixture fixture;
        const auto staged_patch = fixture.directory / L"installed";
        fs::create_directory(staged_patch);
        std::ofstream(staged_patch / L"patch.ini") << "preserved personal settings";
        const auto personal_settings = sha256(staged_patch / L"patch.ini");
        stage_patch_files(package, staged_patch);
        for (const auto* name : {L"xfiles-devtools.exe", L"README.md", L"CHANGELOG.md",
                                 L"docs/developer-tools.md", L"defaults/patch.ini"}) {
            require(sha256(staged_patch / name) == sha256(package / name),
                    "Fresh install omitted or changed a browser or guide file");
        }
        require(!fs::exists(staged_patch / L"xfiles-database.exe") &&
                    sha256(staged_patch / L"patch.ini") == personal_settings,
                "Fresh payload staging copied the old browser or replaced personal settings");
        for (const auto* name : ffmpeg_files) {
            require(sha256(staged_patch / name) == sha256(package / name),
                    "Fresh install changed a codec file");
        }
        fs::remove(staged_patch / L"docs/developer-tools.md");
        const auto rejected = fixture.directory / L"rejected";
        fs::create_directory(rejected);
        bool missing_guide = false;
        try {
            stage_patch_files(staged_patch, rejected);
        } catch (const std::exception& error) {
            missing_guide =
                std::string(error.what()).find("developer-tools.md") != std::string::npos;
        }
        require(missing_guide && fs::is_empty(rejected),
                "Incomplete package staging wrote files before rejecting a missing guide");
        if (argc == 4) {
            const auto staged = stage_game(argv[1], argv[2], argv[3], DisplayMode::Windowed);
            for (const auto* name : ffmpeg_files) {
                if (sha256(staged.directory / name) != sha256(package / name)) {
                    throw std::runtime_error("Installed codec differs from package");
                }
            }
        } else if (argc != 1) {
            throw std::runtime_error("Usage: codec-staging-test [game new-destination media]");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
