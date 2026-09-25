#include "setup/iso.h"
#include "identity.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void make_image(const fs::path& image) {
    std::vector<unsigned char> bytes(28 * 2048);
    const auto put = [&](std::size_t offset, unsigned value) {
        for (unsigned i = 0; i < 4; ++i) {
            bytes[offset + i] = static_cast<unsigned char>(value >> (8 * i));
        }
    };
    const auto descriptor = 16 * 2048;
    bytes[descriptor] = 1;
    std::copy_n("CD001", 5, bytes.begin() + descriptor + 1);
    bytes[descriptor + 6] = 1;
    bytes[descriptor + 129] = 8;
    bytes[descriptor + 156] = 34;
    put(descriptor + 158, 20);
    put(descriptor + 166, 2048);
    const auto record = [&](std::size_t offset, const std::string& name, unsigned start,
                            unsigned length, bool directory) {
        const auto size = (33 + name.size() + 1) & ~std::size_t{1};
        bytes[offset] = static_cast<unsigned char>(size);
        put(offset + 2, start);
        put(offset + 10, length);
        bytes[offset + 25] = directory ? 2 : 0;
        bytes[offset + 32] = static_cast<unsigned char>(name.size());
        std::copy(name.begin(), name.end(), bytes.begin() + offset + 33);
        return size;
    };
    record(20 * 2048, "ENGLISH", 21, 2048, true);
    const auto next = record(21 * 2048, "MININST", 22, 2048, true);
    record(21 * 2048 + next, "MEDINST", 23, 2048, true);
    record(22 * 2048, "XFILES.EXE;1", 25, 8, false);
    record(23 * 2048, "XV", 24, 2048, true);
    record(24 * 2048, "00001.XMV;1", 26, 5, false);
    std::copy_n("fake exe", 8, bytes.begin() + 25 * 2048);
    std::copy_n("movie", 5, bytes.begin() + 26 * 2048);
    std::ofstream output(image, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}
}

int main() {
    try {
        const auto base =
            fs::temp_directory_path() /
            ("xfiles-iso-files-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(base), "Cannot create ISO test directory");

        struct Cleanup {
            fs::path path;

            ~Cleanup() {
                std::error_code ignored;
                fs::remove_all(path, ignored);
            }
        } cleanup{base};

        const auto image = base / "dvd.iso";
        make_image(image);
        auto files = read_iso(image);
        require(files.size() == 2 && files[0].relative == L"ENGLISH/MININST/XFILES.EXE" &&
                    files[1].relative == L"ENGLISH/MEDINST/XV/00001.XMV",
                "Nested DVD directories were not read correctly");
        const std::map<std::wstring, MediaRecord> catalog{
            {L"xfiles.exe", {8, sha256(image, 25 * 2048, 8)}},
            {L"xv/00001.xmv", {5, sha256(image, 26 * 2048, 5)}}};
        const auto selected = select_iso_files(files, catalog);
        require(selected.size() == 2 && selected[0].relative == L"xfiles.exe" &&
                    selected[1].relative == L"xv/00001.xmv" && selected[1].offset == 26 * 2048,
                "DVD catalog mapping lost a path or ISO extent");
        const auto output = base / "movie.xmv";
        copy_media_file(selected[1], output, [] { return true; });
        require(sha256(output) == catalog.at(L"xv/00001.xmv").sha256,
                "The selected DVD extent did not copy correctly");
        // The same catalog matching also handles the root and MININST/MEDINST CD layout.
        files[0].relative = L"MININST/XFILES.EXE";
        files[1].relative = L"MEDINST/XV/00001.XMV";
        require(select_iso_files(files, catalog).size() == 2, "CD install layers stopped matching");
        files[0].relative = L"XFILES.EXE";
        files[1].relative = L"XV/00001.XMV";
        require(select_iso_files(files, catalog).size() == 2, "Root game files stopped matching");
        auto damaged = files[1];
        damaged.offset = 25 * 2048; // Correct size, wrong contents.
        files.insert(files.begin(), damaged);
        const auto repaired = select_iso_files(files, catalog);
        require(repaired[1].offset == 26 * 2048, "A damaged duplicate hid a verified copy");
        std::reverse(files.begin(), files.end());
        require(select_iso_files(files, catalog)[1].offset == 26 * 2048,
                "A damaged duplicate replaced a verified copy");
        for (int failure = 0; failure < 3; ++failure) {
            auto invalid = read_iso(image);
            if (failure == 0) {
                invalid.pop_back();
            } else if (failure == 1) {
                ++invalid[0].size;
            } else {
                invalid[0].relative = L"GERMAN/MININST/XFILES.EXE";
            }
            bool rejected = false;
            try {
                select_iso_files(invalid, catalog);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Incomplete or wrong-language image accepted");
        }
        bool cancelled = false;
        try {
            copy_media_file(selected[1], base / "cancelled.xmv", [] { return false; });
        } catch (const std::exception&) {
            cancelled = true;
        }
        require(cancelled, "ISO copy ignored cancellation");
        std::cout << "DVD/CD ISO paths, extents, duplicate checks and cancellation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
