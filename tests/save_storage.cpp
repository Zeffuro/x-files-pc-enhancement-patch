#include "saves/storage.h"
#include "saves/header.h"
#include "saves/paths.h"
#include "saves/catalog.h"
#include "dispatch.h"

#include <windows.h>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {
void fixture(const fs::path& path, const char* text) {
    std::array<char, 24> header{};
    header[3] = 5;
    header[22] = 5;
    header[23] = 1;
    std::ofstream output(path, std::ios::binary);
    output.write(header.data(), header.size());
    output << text;
}
}

int main() {
    try {
        const auto root = fs::temp_directory_path() /
                          (L"xfiles-save-storage-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()));
        fs::create_directory(root);
        fixture(root / "QUICKSAVE.x", "old");
        fixture(root / L"Saved game.X", "native");
        std::ofstream(root / "unrelated.x") << "not a saved game";
        const auto directory = saves::prepare_directory(root);
        test::require(saves::redirected_path(directory, root / L"Saved game.X") ==
                          directory / L"Saved game.X",
                      "Native root save path was not redirected");
        test::require(saves::redirected_path(directory, directory / L"QUICKSAVE.x").empty() &&
                          saves::redirected_path(directory, root / L"XV" / L"movie.x").empty() &&
                          saves::redirected_path(directory, root / L"XFiles.exe").empty() &&
                          saves::redirected_path(directory, root / L".." / L"outside.x").empty(),
                      "Save path mapping affected unrelated files");
        test::require(directory == root / "saves", "Wrong save directory");
        const auto catalog = saves::read_catalog(root);
        test::require(catalog.entries.size() == 2 && catalog.entries.front().name == L"QUICKSAVE" &&
                          catalog.entries.front().header_supported &&
                          catalog.entries.front().modified.size() == 16 &&
                          catalog.entries.front().modified[4] == L'-',
                      "Save discovery lost names, header status or international dates");
        test::require(saves::page(catalog, 0, 1).size() == 1 &&
                          saves::page(catalog, 1, 1).front().name == L"Saved game" &&
                          saves::page(catalog, 2, 1).empty() &&
                          saves::page(catalog, SIZE_MAX, 6).empty() &&
                          saves::page(catalog, 0, 0).empty(),
                      "Save pages lost entries or accepted invalid bounds");
        fs::create_directory(directory / "nested.x");
        fixture(directory / "nested.x" / "hidden.x", "nested");
        std::ofstream(directory / "damaged.x") << "bad";
        const auto mixed = saves::read_catalog(root);
        test::require(mixed.entries.size() == 3 && mixed.skipped == 1 &&
                          !mixed.entries.front().header_supported,
                      "Save discovery hid damaged files or recursed into subfolders");
        test::require(saves::read_catalog(root / "missing").entries.empty() &&
                          !fs::exists(root / "missing"),
                      "Read-only save discovery created a folder");
        test::require(saves::supported_header(directory / "QUICKSAVE.x") &&
                          saves::supported_header(directory / L"Saved game.X"),
                      "Existing quick or named saves were not copied");
        test::require(fs::exists(root / "QUICKSAVE.x") && fs::exists(root / L"Saved game.X"),
                      "Original saves were removed during migration");
        test::require(!fs::exists(directory / "unrelated.x"), "Unrelated file was migrated");
        fixture(directory / "QUICKSAVE.x", "newer contents");
        const auto size = fs::file_size(directory / "QUICKSAVE.x");
        saves::prepare_directory(root);
        test::require(fs::file_size(directory / "QUICKSAVE.x") == size,
                      "Migration replaced a newer save");
        fs::remove(directory / L"Saved game.X");
        saves::prepare_directory(root);
        test::require(!fs::exists(directory / L"Saved game.X"),
                      "Migration restored a save deleted after the first launch");
        fs::create_directory(root / "blocked");
        std::ofstream(root / "blocked" / "saves") << "keep me";
        bool rejected = false;
        try {
            saves::prepare_directory(root / "blocked");
        } catch (const std::exception&) {
            rejected = true;
        }
        test::require(rejected, "A file occupying the saves directory was accepted");
        std::cout << "Save migration preserves originals, newer saves and unrelated files.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
