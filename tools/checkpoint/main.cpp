#include "preferences/store.h"
#include "saves/header.h"

#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) {
            std::cerr << "Usage: xfiles-checkpoint <save.x> <installed-game-folder>\n"
                         "Copies a checkpoint to the quick-load slot. Close the game first.\n";
            return 1;
        }
        const auto source = std::filesystem::canonical(argv[1]);
        const auto folder = std::filesystem::canonical(argv[2]);
        if (!saves::supported_header(source)) {
            throw std::runtime_error("The file does not have a supported X-Files save header.");
        }
        if (!std::filesystem::exists(folder / L"preferences.ini") ||
            !std::filesystem::exists(folder / L"XFilesPlay.exe")) {
            throw std::runtime_error("Choose an installed portable game folder.");
        }
        preferences::Store lock(folder / L"preferences.ini");
        const auto target = folder / L"QUICKSAVE.x";
        if (std::filesystem::exists(target)) {
            if (std::filesystem::equivalent(source, target)) {
                throw std::runtime_error("The checkpoint is already in the quick-load slot.");
            }
            unsigned index = 1;
            std::filesystem::path backup;
            do {
                backup = folder / (L"QUICKSAVE.import-backup-" + std::to_wstring(index++) + L".x");
            } while (std::filesystem::exists(backup));
            std::filesystem::copy_file(target, backup);
            std::wcout << L"Preserved existing quick-save: " << backup << L'\n';
        }
        const auto temporary = folder / L"QUICKSAVE.importing";
        std::filesystem::copy_file(source, temporary);
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Could not install checkpoint; the temporary copy was kept.");
        }
        std::cout
            << "Checkpoint installed. Start the game and press F9 at the main menu.\n"
               "Only the header was checked; the game validates the saved state when loading.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
