#include "saves/slots.h"
#include "saves/catalog.h"
#include "dispatch.h"
#include <windows.h>
#include <array>
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

template <typename F> void rejects(F call) {
    bool rejected = false;
    try {
        call();
    } catch (const std::exception&) {
        rejected = true;
    }
    test::require(rejected, "Invalid slot operation was accepted");
}
}

int main() {
    try {
        const auto root =
            fs::temp_directory_path() / (L"xfiles-slots-" + std::to_wstring(GetCurrentProcessId()) +
                                         L"-" + std::to_wstring(GetTickCount64()));
        fs::create_directory(root);
        test::require(!saves::read_slot(root, 600).occupied && !fs::exists(root / "saves"),
                      "Reading empty slots wrote to disk");
        rejects([&] { saves::read_slot(root, 0); });
        rejects([&] { saves::read_slot(root, 601); });
        fixture(root / "prepared.x", "first");
        const saves::Thumbnail image{2, 1, {1, 2, 3, 4, 5, 6, 7, 8}};
        saves::write_slot(root, 1, L"倉庫 - Warehouse", root / "prepared.x", image);
        const auto first = saves::read_slot(root, 1);
        test::require(first.occupied && first.readable && first.name == L"倉庫 - Warehouse" &&
                          first.date.size() == 16,
                      "Save name, date or data lost");
        const auto thumb = saves::read_thumbnail(first.thumbnail);
        test::require(thumb.width == 2 && thumb.height == 1 && thumb.pixels == image.pixels,
                      "Thumbnail changed");
        std::ofstream(root / "invalid.x") << "not a save";
        rejects([&] { saves::write_slot(root, 1, L"bad", root / "invalid.x", {}); });
        rejects(
            [&] { saves::write_slot(root, 1, std::wstring(81, L'x'), root / "prepared.x", {}); });
        rejects([&] { saves::write_slot(root, 1, L"bad", root / "prepared.x", {1000, 1, {}}); });
        test::require(saves::read_slot(root, 1).file == first.file && fs::exists(first.file),
                      "Rejected update damaged the previous save");
        const auto locked =
            CreateFileW((first.file.parent_path() / L"current").c_str(), GENERIC_READ,
                        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        test::require(locked != INVALID_HANDLE_VALUE, "Cannot lock the publication fixture");
        rejects([&] { saves::write_slot(root, 1, L"Cannot publish", root / "prepared.x", image); });
        rejects([&] { saves::delete_slot(root, 1); });
        CloseHandle(locked);
        test::require(saves::read_slot(root, 1).file == first.file && fs::exists(first.file),
                      "Failed publication replaced the previous save");
        fixture(root / "prepared.x", "replacement with longer state");
        saves::write_slot(root, 1, L"Second", root / "prepared.x", {});
        const auto second = saves::read_slot(root, 1);
        test::require(second.readable && second.name == L"Second" && second.file != first.file &&
                          !fs::exists(first.file) &&
                          fs::file_size(second.file) == fs::file_size(root / "prepared.x"),
                      "Slot replacement failed or retained obsolete data");
        fixture(root / "saves" / "Existing name.x", "legacy");
        test::require(saves::read_catalog(root).entries.size() == 1 &&
                          saves::read_catalog(root).entries.front().name == L"Existing name",
                      "Slots changed loose-save discovery");
        fs::create_directory(root / "saves" / "slots" / "2");
        fs::create_directory(root / "saves" / "slots" / "2" / "current");
        rejects([&] { saves::write_slot(root, 2, L"blocked", root / "prepared.x", {}); });
        test::require(saves::read_slot(root, 2).occupied && !saves::read_slot(root, 2).readable,
                      "Broken metadata was treated as an empty slot");
        std::ofstream(root / "bad.thumb", std::ios::binary)
            .write("\xff\xff\xff\xff\xff\xff\xff\xff", 8);
        test::require(saves::read_thumbnail(root / "bad.thumb").pixels.empty(),
                      "Oversized thumbnail accepted");
        const auto unrelated = second.file.parent_path() / "notes.txt";
        std::ofstream(unrelated) << "Keep me";
        saves::delete_slot(root, 1);
        test::require(!saves::read_slot(root, 1).occupied && !fs::exists(second.file) &&
                          fs::exists(unrelated) && fs::exists(root / "saves" / "Existing name.x"),
                      "Slot deletion affected unrelated files or left a published save");
        saves::delete_slot(root, 1);
        rejects([&] { saves::delete_slot(root, 0); });
        std::cout
            << "Fixed slots preserve names and data across replacement and rejected writes.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
