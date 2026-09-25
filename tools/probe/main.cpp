#include "launcher/process.h"
#include "launcher/staging.h"
#include "launcher/preferences.h"
#include "launcher/session.h"
#include "launcher/application.h"
#include "runtime.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int launch(const std::filesystem::path& game, const std::filesystem::path& destination,
           const std::filesystem::path& media_root, DisplayMode mode, bool portable, bool probe,
           bool fresh) {
    const Session session{std::filesystem::canonical(media_root), portable};
    const auto staged = stage_game(game, destination, session.media, mode);
    if (portable) {
        stage_preferences(game, staged.directory, !fresh);
    }
    save_session(staged.directory, session);
    return execute_game(staged, session, probe);
}

}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring(argv[1]) == L"resume") {
            return resume_game(argv[2]);
        }
        if (argc < 4 || (std::wstring(argv[1]) != L"probe" && std::wstring(argv[1]) != L"play")) {
            std::cerr << "Usage: xfiles-probe <play|probe> <game-directory> <new-output-directory> "
                         "[--windowed|--borderless] [--portable|--registry] [--fresh] [--media "
                         "<directory>]\n"
                         "       xfiles-probe resume <output-directory>\n";
            return 1;
        }

        DisplayMode mode = DisplayMode::Borderless;
        bool portable = true;
        bool fresh = false;
        std::filesystem::path media = argv[2];
        for (int i = 4; i < argc; ++i) {
            const std::wstring option = argv[i];
            if (option == L"--windowed") {
                mode = DisplayMode::Windowed;
            } else if (option == L"--borderless") {
                mode = DisplayMode::Borderless;
            } else if (option == L"--portable") {
                portable = true;
            } else if (option == L"--registry") {
                portable = false;
            } else if (option == L"--media" && i + 1 < argc) {
                media = argv[++i];
            } else if (option == L"--fresh") {
                fresh = true;
            } else {
                throw std::runtime_error("Unknown option.");
            }
        }
        if (fresh && !portable) {
            throw std::runtime_error("--fresh requires portable preferences.");
        }
        return launch(argv[2], argv[3], media, mode, portable, std::wstring(argv[1]) == L"probe",
                      fresh);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
