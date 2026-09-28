#include "setup/iso.h"
#include "identity.h"
#include "platform/copy_file.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace fs = std::filesystem;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void make_image(const fs::path& image) {
    std::vector<unsigned char> bytes(33 * 2048);
    const auto put = [&](std::size_t offset, unsigned value) {
        for (unsigned i = 0; i < 4; ++i) {
            bytes[offset + i] = static_cast<unsigned char>(value >> (8 * i));
        }
    };
    const auto put_both = [&](std::size_t offset, unsigned value) {
        put(offset, value);
        for (unsigned i = 0; i < 4; ++i) {
            bytes[offset + 4 + i] = static_cast<unsigned char>(value >> (8 * (3 - i)));
        }
    };
    const auto descriptor = 16 * 2048;
    bytes[descriptor] = 1;
    std::copy_n("CD001", 5, bytes.begin() + descriptor + 1);
    bytes[descriptor + 6] = 1;
    bytes[descriptor + 129] = 8;
    bytes[descriptor + 156] = 34;
    put_both(descriptor + 158, 20);
    put_both(descriptor + 166, 2048);
    const auto record = [&](std::size_t offset, const std::string& name, unsigned start,
                            unsigned length, bool directory) {
        const auto size = (33 + name.size() + 1) & ~std::size_t{1};
        bytes[offset] = static_cast<unsigned char>(size);
        put_both(offset + 2, start);
        put_both(offset + 10, length);
        bytes[offset + 25] = directory ? 2 : 0;
        bytes[offset + 32] = static_cast<unsigned char>(name.size());
        std::copy(name.begin(), name.end(), bytes.begin() + offset + 33);
        return size;
    };
    record(20 * 2048, "ENGLISH", 21, 2048, true);
    const auto next = record(21 * 2048, "MININST", 22, 2048, true);
    const auto after_medium = record(21 * 2048 + next, "MEDINST", 23, 2048, true);
    record(21 * 2048 + next + after_medium, "VOB", 29, 2048, true);
    const auto core = record(22 * 2048, "XFILES.EXE;1", 30, 8, false);
    bytes[22 * 2048 + 25] = 4;
    record(22 * 2048 + core, "XFILES.EXE;1", 25, 8, false);
    record(23 * 2048, "XV", 24, 2048, true);
    constexpr unsigned movie_size = 5000;
    record(24 * 2048, "00001.XMV;1", 26, movie_size, false);
    record(29 * 2048, "TEASER.VOB;1", 30, 7, false);
    for (unsigned sector = 30; sector <= 32; ++sector) {
        std::copy_n("dvd vob", 7, bytes.begin() + sector * 2048);
    }
    std::copy_n("fake exe", 8, bytes.begin() + 25 * 2048);
    for (unsigned i = 0; i < movie_size; ++i) {
        bytes[26 * 2048 + i] = static_cast<unsigned char>((i * 37 + 11) & 0xff);
    }
    std::ofstream output(image, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

void make_raw(const fs::path& image, const fs::path& bin, const fs::path& cue) {
    std::ifstream input(image, std::ios::binary);
    const std::vector<unsigned char> cooked((std::istreambuf_iterator<char>(input)), {});
    require(cooked.size() % 2048 == 0, "Cooked fixture is not sector aligned");
    std::vector<unsigned char> raw(cooked.size() / 2048 * 2352);
    for (std::size_t sector = 0; sector < cooked.size() / 2048; ++sector) {
        const auto start = sector * 2352;
        std::fill(raw.begin() + static_cast<std::ptrdiff_t>(start + 1),
                  raw.begin() + static_cast<std::ptrdiff_t>(start + 11),
                  static_cast<unsigned char>(0xff));
        raw[start + 15] = 1;
        std::copy_n(cooked.begin() + static_cast<std::ptrdiff_t>(sector * 2048), 2048,
                    raw.begin() + static_cast<std::ptrdiff_t>(start + 16));
    }
    std::ofstream output(bin, std::ios::binary);
    output.write(reinterpret_cast<const char*>(raw.data()),
                 static_cast<std::streamsize>(raw.size()));
    std::ofstream(cue) << "FILE \"" << bin.filename().string() << "\" BINARY\n"
                       << "  TRACK 01 MODE1/2352\n"
                       << "    INDEX 01 00:00:00\n";
}

void expect_image_error(const fs::path& cue, std::string_view message) {
    bool rejected = false;
    try {
        read_iso(cue);
    } catch (const std::exception& error) {
        rejected = std::string_view(error.what()).find(message) != std::string_view::npos;
    }
    require(rejected, "Malformed CUE/BIN fixture was not rejected as expected");
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
        require(files.size() == 3 && files[0].relative == L"ENGLISH/MININST/XFILES.EXE" &&
                    files[1].relative == L"ENGLISH/MEDINST/XV/00001.XMV" &&
                    files[2].relative == L"ENGLISH/VOB/TEASER.VOB",
                "Nested DVD directories were not read correctly");
        const std::map<std::wstring, MediaRecord> catalog{
            {L"xfiles.exe", {8, sha256(image, 25 * 2048, 8)}},
            {L"xv/00001.xmv", {5000, sha256(image, 26 * 2048, 5000)}},
            {L"vob/teaser.vob", {7, sha256(image, 30 * 2048, 7)}}};
        const auto selected = select_iso_files(files, catalog);
        require(selected.size() == 3 && selected[0].relative == L"vob/teaser.vob" &&
                    selected[0].offset == 30 * 2048 && selected[1].relative == L"xfiles.exe" &&
                    selected[2].relative == L"xv/00001.xmv" && selected[2].offset == 26 * 2048,
                "DVD catalog mapping lost a path or ISO extent");
        const auto vob_output = base / "teaser.vob";
        copy_media_file(selected[0], vob_output, [] { return true; });
        require(sha256(vob_output) == catalog.at(L"vob/teaser.vob").sha256,
                "The DVD VOB extent did not copy correctly");
        const auto output = base / "movie.xmv";
        copy_media_file(selected[2], output, [] { return true; });
        require(sha256(output) == catalog.at(L"xv/00001.xmv").sha256,
                "The selected multi-sector DVD extent did not copy correctly");

        const auto bin = base / "disc.bin";
        const auto cue = base / "disc.cue";
        make_raw(image, bin, cue);
        const auto raw_files = read_iso(cue);
        require(raw_files.size() == files.size() && raw_files[2].geometry.sector_size == 2352 &&
                    raw_files[2].geometry.payload_offset == 16 &&
                    media_file_sha256(raw_files[1]) == catalog.at(L"xv/00001.xmv").sha256 &&
                    media_file_sha256(raw_files[2]) == catalog.at(L"vob/teaser.vob").sha256,
                "MODE1/2352 payload did not match the cooked ISO file");
        const auto raw_selected = select_iso_files(raw_files, catalog);
        const auto raw_output = base / "raw-movie.xmv";
        copy_media_file(raw_selected[2], raw_output, [] { return true; });
        require(sha256(raw_output) == catalog.at(L"xv/00001.xmv").sha256,
                "Multi-sector BIN payload copy included raw sector bytes");
        const auto raw_vob_output = base / "raw-teaser.vob";
        copy_media_file(raw_selected[0], raw_vob_output, [] { return true; });
        require(sha256(raw_vob_output) == catalog.at(L"vob/teaser.vob").sha256,
                "BIN/CUE VOB extent did not copy correctly");
        std::ofstream(image, std::ios::binary | std::ios::app).put('\0');
        const auto trailing_files = read_iso(image);
        const auto trailing_output = base / "trailing-movie.xmv";
        copy_media_file(trailing_files[1], trailing_output, [] { return true; });
        require(sha256(trailing_output) == catalog.at(L"xv/00001.xmv").sha256,
                "A harmless trailing ISO byte changed extent handling");
        // The same catalog matching also handles the root and MININST/MEDINST CD layout.
        files[0].relative = L"MININST/XFILES.EXE";
        files[1].relative = L"MEDINST/XV/00001.XMV";
        require(select_iso_files(files, catalog).size() == 3, "CD install layers stopped matching");
        files[0].relative = L"XFILES.EXE";
        files[1].relative = L"XV/00001.XMV";
        require(select_iso_files(files, catalog).size() == 3, "Root game files stopped matching");
        auto damaged = files[1];
        damaged.offset = 25 * 2048; // Correct size, wrong contents.
        files.insert(files.begin(), damaged);
        const auto repaired = select_iso_files(files, catalog);
        require(repaired[2].offset == 26 * 2048, "A damaged duplicate hid a verified copy");
        std::reverse(files.begin(), files.end());
        require(select_iso_files(files, catalog)[2].offset == 26 * 2048,
                "A damaged duplicate replaced a verified copy");
        auto layered = files;
        auto medium_vob = files[0];
        medium_vob.relative = L"ENGLISH/MEDINST/VOB/TEASER.VOB";
        medium_vob.offset = 31 * 2048;
        auto minimum_vob = medium_vob;
        minimum_vob.relative = L"ENGLISH/MININST/VOB/TEASER.VOB";
        minimum_vob.offset = 32 * 2048;
        layered.insert(layered.begin(), medium_vob);
        layered.push_back(minimum_vob);
        require(select_iso_files(layered, catalog)[0].offset == 32 * 2048,
                "MININST VOB did not take precedence over root and MEDINST");
        layered.pop_back();
        require(select_iso_files(layered, catalog)[0].offset == 30 * 2048,
                "Root VOB did not take precedence over MEDINST");
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

        const auto missing = base / "missing.cue";
        std::ofstream(missing) << "FILE \"missing.bin\" BINARY\nTRACK 01 MODE1/2352\n"
                                  "INDEX 01 00:00:00\n";
        expect_image_error(missing, "missing");
        const auto multiple = base / "multiple.cue";
        std::ofstream(multiple) << "FILE \"disc.bin\" BINARY\nTRACK 01 MODE1/2352\n"
                                   "INDEX 01 00:00:00\nTRACK 02 AUDIO\nINDEX 01 00:10:00\n";
        expect_image_error(multiple, "Unsupported");
        const auto pregap = base / "pregap.cue";
        std::ofstream(pregap) << "FILE \"disc.bin\" BINARY\nTRACK 01 MODE1/2352\n"
                                 "PREGAP 00:02:00\nINDEX 01 00:00:00\n";
        expect_image_error(pregap, "Unsupported");
        const auto nonzero_index = base / "nonzero-index.cue";
        std::ofstream(nonzero_index) << "FILE \"disc.bin\" BINARY\nTRACK 01 MODE1/2352\n"
                                        "INDEX 01 00:02:00\n";
        expect_image_error(nonzero_index, "Unsupported");
        const auto mode2 = base / "mode2.cue";
        std::ofstream(mode2) << "FILE \"disc.bin\" BINARY\nTRACK 01 MODE2/2352\n"
                                "INDEX 01 00:00:00\n";
        expect_image_error(mode2, "PlayStation");
        const auto escaped = base / "escaped.cue";
        std::ofstream(escaped) << "FILE \"../disc.bin\" BINARY\nTRACK 01 MODE1/2352\n"
                                  "INDEX 01 00:00:00\n";
        expect_image_error(escaped, "same folder");
        const auto absolute = base / "absolute.cue";
        std::ofstream(absolute) << "FILE \"" << bin.string()
                                << "\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n";
        expect_image_error(absolute, "same folder");
        const auto short_bin = base / "short.bin";
        std::ofstream(short_bin, std::ios::binary).write("short", 5);
        const auto truncated = base / "truncated.cue";
        std::ofstream(truncated) << "FILE \"short.bin\" BINARY\nTRACK 01 MODE1/2352\n"
                                    "INDEX 01 00:00:00\n";
        expect_image_error(truncated, "truncated");
        const auto bad_header = base / "bad-header.bin";
        platform::copy_file(bin, bad_header);
        {
            std::fstream corrupt(bad_header, std::ios::binary | std::ios::in | std::ios::out);
            corrupt.seekp(16 * 2352 + 15);
            corrupt.put('\2');
        }
        const auto bad_header_cue = base / "bad-header.cue";
        std::ofstream(bad_header_cue)
            << "FILE \"bad-header.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n";
        expect_image_error(bad_header_cue, "headers");

        const auto mdf_folder = base / "mdf";
        fs::create_directory(mdf_folder);
        std::ofstream(mdf_folder / "disc.mdf") << "unsupported";
        bool mdf_rejected = false;
        try {
            inspect_iso_folder(mdf_folder);
        } catch (const std::exception& error) {
            mdf_rejected = std::string_view(error.what()).find("MDF/MDS") != std::string_view::npos;
        }
        require(mdf_rejected, "MDF source did not return a helpful unsupported-format error");
        std::cout << "ISO and BIN/CUE paths, geometry, malformed inputs and copying passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
