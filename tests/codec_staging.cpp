#include "launcher/staging.h"
#include "ffmpeg_files.h"

#include <windows.h>
#include <iostream>
#include <stdexcept>

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
