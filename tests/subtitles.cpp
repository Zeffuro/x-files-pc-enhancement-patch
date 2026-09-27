#include "subtitles.h"

#include <windows.h>
#include <ioapi.h>
#include <iowin32.h>
#include <zip.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using Data = std::vector<std::uint8_t>;

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void archive(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& files,
             unsigned attributes = 0) {
    zlib_filefunc64_def io{};
    fill_win32_filefunc64W(&io);
    const auto file = zipOpen2_64(path.c_str(), APPEND_STATUS_CREATE, nullptr, &io);
    require(file != nullptr, "Cannot create ZIP fixture");
    for (const auto& [name, text] : files) {
        zip_fileinfo info{};
        info.external_fa = attributes;
        require(zipOpenNewFileInZip64(file, name.c_str(), &info, nullptr, 0, nullptr, 0, nullptr,
                                      Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0) == ZIP_OK &&
                    zipWriteInFileInZip(file, text.data(), static_cast<unsigned>(text.size())) ==
                        ZIP_OK &&
                    zipCloseFileInZip(file) == ZIP_OK,
                "Cannot write ZIP fixture");
    }
    require(zipClose(file, nullptr) == ZIP_OK, "Cannot close ZIP fixture");
}

void word(Data& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

Data words(std::initializer_list<std::uint32_t> values) {
    Data result;
    for (const auto value : values) {
        word(result, value);
    }
    return result;
}

void atom(Data& bytes, const char* type, const Data& body) {
    word(bytes, static_cast<std::uint32_t>(body.size() + 8));
    bytes.insert(bytes.end(), type, type + 4);
    bytes.insert(bytes.end(), body.begin(), body.end());
}

Data movie_bytes(bool text_enabled = true, std::uint32_t text_scale = 1000, bool overlay = false) {
    Data file;
    Data payload{1, 2, 3, 4, 0, 5, 'H', 'e', 'l', 'l', 'o', 0, 0};
    if (overlay) {
        payload.insert(payload.end(), {0, 3, 'B', 'y', 'e', 0, 0});
    }
    atom(file, "mdat", payload);
    Data video_desc(78);
    video_desc[7] = 1;
    video_desc[25] = video_desc[27] = 4;
    Data video_descriptions = words({0, 1});
    atom(video_descriptions, "rpza", video_desc);
    Data video_samples;
    atom(video_samples, "stsd", video_descriptions);
    atom(video_samples, "stco", words({0, 1, 8}));
    atom(video_samples, "stsc", words({0, 1, 1, 1, 1}));
    atom(video_samples, "stsz", words({0, 4, 1}));
    atom(video_samples, "stts", words({0, 1, 1, 450}));
    Data video_info;
    atom(video_info, "stbl", video_samples);
    Data video_media;
    atom(video_media, "mdhd", words({0, 0, 0, 1000, 450}));
    atom(video_media, "hdlr", words({0, 0, 0x76696465}));
    atom(video_media, "minf", video_info);
    Data video_track;
    atom(video_track, "tkhd", words({1, 0, 0, 1, 0, 450}));
    atom(video_track, "mdia", video_media);

    Data text_desc = words({0, 1});
    atom(text_desc, "text", words({0, 1}));
    Data text_samples;
    atom(text_samples, "stsd", text_desc);
    atom(text_samples, "stco", words({0, 1, 12}));
    atom(text_samples, "stsc", words({0, 1, 1, 2, 1}));
    atom(text_samples, "stsz", words({0, 0, 2, 7, 2}));
    atom(text_samples, "stts", words({0, 1, 2, text_scale / 10}));
    Data text_info;
    atom(text_info, "stbl", text_samples);
    Data text_media;
    atom(text_media, "mdhd", words({0, 0, 0, text_scale, text_scale / 5}));
    atom(text_media, "hdlr", words({0, 0, 0x74657874}));
    atom(text_media, "minf", text_info);
    Data edits;
    atom(edits, "elst", words({0, 2, 50, 0xffffffff, 65536, 400, 0, 65536}));
    Data text_track;
    atom(text_track, "tkhd", words({text_enabled ? 1u : 0u, 0, 0, 2, 0, 450}));
    atom(text_track, "edts", edits);
    atom(text_track, "mdia", text_media);
    Data movie;
    atom(movie, "mvhd", words({0, 0, 0, 1000, 450}));
    atom(movie, "trak", video_track);
    atom(movie, "trak", text_track);
    if (overlay) {
        Data overlay_samples;
        atom(overlay_samples, "stsd", text_desc);
        atom(overlay_samples, "stco", words({0, 1, 21}));
        atom(overlay_samples, "stsc", words({0, 1, 1, 2, 1}));
        atom(overlay_samples, "stsz", words({0, 0, 2, 5, 2}));
        atom(overlay_samples, "stts", words({0, 1, 2, text_scale / 10}));
        Data overlay_info;
        atom(overlay_info, "stbl", overlay_samples);
        Data overlay_media;
        atom(overlay_media, "mdhd", words({0, 0, 0, text_scale, text_scale / 5}));
        atom(overlay_media, "hdlr", words({0, 0, 0x74657874}));
        atom(overlay_media, "minf", overlay_info);
        Data overlay_track;
        Data tkhd(84);
        tkhd[3] = 1;
        tkhd[15] = 3;
        tkhd[23] = 450 & 0xff;
        tkhd[32] = tkhd[33] = 0xff;
        atom(overlay_track, "tkhd", tkhd);
        atom(overlay_track, "edts", edits);
        atom(overlay_track, "mdia", overlay_media);
        atom(movie, "trak", overlay_track);
    }
    atom(file, "moov", movie);
    return file;
}

void write(const fs::path& path, const Data& data) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
}

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void rejected(const std::function<void()>& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Malformed subtitle pack was accepted.");
}

}

int main(int argc, char** argv) {
    const auto base = fs::temp_directory_path() /
                      ("xfiles-subtitles-test-" + std::to_string(GetCurrentProcessId()));
    try {
        fs::create_directories(base / "game" / "XV");
        const auto movie_path = base / "game" / "XV" / "123.xmv";
        write(movie_path, movie_bytes());
        write(base / "game" / "XV" / "bad.xmv", "opaque");
        const auto movie = media::Movie::open(movie_path);
        const auto native = media::subtitles::native_cues(movie);
        require(native.size() == 1 && native[0].begin == 50 && native[0].end == 150 &&
                    native[0].text == L"Hello",
                "Edit or clear sample timing changed.");
        const auto disabled_path = base / "disabled.xmv";
        write(disabled_path, movie_bytes(false, 2000));
        const auto disabled = media::Movie::open(disabled_path);
        const auto disabled_cues = media::subtitles::native_cues(disabled);
        require(disabled_cues.size() == 1 && disabled_cues[0].begin == 50 &&
                    disabled_cues[0].end == 150 && disabled_cues[0].text == L"Hello",
                "Disabled text or different track timescale lost exported cue timing.");
        const auto overlay_path = base / "overlay.xmv";
        write(overlay_path, movie_bytes(true, 2000, true));
        const auto overlay_movie = media::Movie::open(overlay_path);
        const auto overlay_cues = media::subtitles::native_cues(overlay_movie);
        require(overlay_cues.size() == 1 && overlay_cues[0].begin == 50 &&
                    overlay_cues[0].end == 150 && overlay_cues[0].text == L"Bye",
                "Higher-priority text layer or clear timing changed.");

        fs::create_directories(base / "cancelled");
        rejected([&] {
            media::subtitles::export_pack(base / "game", base / "cancelled",
                                          [&](std::size_t, std::size_t) {
                                              write(base / "cancelled" / "unrelated.txt", "keep");
                                              return false;
                                          });
        });
        require(read(base / "cancelled" / "unrelated.txt") == "keep",
                "Cancelled export removed an unrelated file.");

        fs::create_directories(base / "pack");
        const auto exported = media::subtitles::export_pack(base / "game", base / "pack");
        require(exported.movies == 1 && exported.cues == 1 && exported.skipped == 1,
                "Export did not count readable and opaque movies.");
        require(read(base / "pack" / "XV" / "123.srt").find("00:00:00,050 --> 00:00:00,150") !=
                    std::string::npos,
                "Exported SRT lost edit timing.");
        require(read(base / "pack" / "skipped.txt").find("XV/bad.xmv") != std::string::npos,
                "Opaque movie omission was not reported.");
        const auto generation = media::subtitles::install_generation();
        const auto installed = media::subtitles::install_pack(base / "game", base / "pack");
        require(installed.movies == 1 && installed.cues == 1, "Valid pack was not installed.");
        require(media::subtitles::install_generation() == generation + 1,
                "Successful install did not invalidate cached subtitles.");
        require(media::subtitles::load_override(base / "game", "XV/123.xmv", movie_path)
                        ->front()
                        .text == L"Hello",
                "Installed override was not loaded.");
        require(
            media::subtitles::caption_at(
                *media::subtitles::load_override(base / "game", "XV/123.xmv", movie_path), 49, 1000)
                    .empty() &&
                media::subtitles::caption_at(
                    *media::subtitles::load_override(base / "game", "XV/123.xmv", movie_path), 50,
                    1000) == L"Hello",
            "Override did not use the movie clock.");

        const auto original = read(base / "game" / "subtitles" / "XV" / "123.srt");
        const auto listing = read(base / "pack" / "manifest.tsv");
        std::string windows_listing;
        for (const auto character : listing) {
            if (character == '\n') {
                windows_listing += '\r';
            }
            windows_listing += character;
        }
        write(base / "pack" / "manifest.tsv", windows_listing);
        require(media::subtitles::install_pack(base / "game", base / "pack").movies == 1,
                "Windows line endings rejected a valid subtitle manifest");
        write(base / "pack" / "manifest.tsv", listing);
        const auto first_row = listing.substr(listing.find('\n') + 1);
        auto duplicate = first_row;
        for (auto& character : duplicate) {
            if (character == 'X' || character == 'V') {
                character += 'a' - 'A';
            }
        }
        write(base / "pack" / "manifest.tsv", listing + duplicate);
        rejected([&] { media::subtitles::install_pack(base / "game", base / "pack"); });
        require(read(base / "game" / "subtitles" / "XV" / "123.srt") == original,
                "Case-alias manifest replaced installed subtitles.");
        write(base / "pack" / "manifest.tsv", listing);
        auto reserved = first_row;
        reserved.replace(reserved.rfind("XV/123.srt"), 10, "XV/CON.srt");
        write(base / "pack" / "manifest.tsv", listing.substr(0, listing.find('\n') + 1) + reserved);
        rejected([&] { media::subtitles::install_pack(base / "game", base / "pack"); });
        write(base / "pack" / "manifest.tsv", listing);
        write(base / "pack" / "XV" / "123.srt", "1\n00:00:00,050 --> 00:00:00,150\nEdited\n\n");
        rejected([&] {
            media::subtitles::install_pack(
                base / "game", base / "pack",
                [](std::size_t done, std::size_t total) { return done + 1 < total; });
        });
        require(read(base / "game" / "subtitles" / "XV" / "123.srt") == original,
                "Cancellation replaced installed subtitles.");
        write(base / "pack" / "XV" / "123.srt", "1\n00:00:01,000 --> 00:00:02,000\nBad\n\n");
        rejected([&] { media::subtitles::install_pack(base / "game", base / "pack"); });
        require(read(base / "game" / "subtitles" / "XV" / "123.srt") == original,
                "Invalid timing replaced installed subtitles.");
        write(base / "pack" / "XV" / "123.srt", "");
        media::subtitles::install_pack(base / "game", base / "pack");
        require(!media::subtitles::load_override(base / "game", "XV/123.xmv", movie_path),
                "Blank SRT suppressed native captions.");
        write(base / "pack" / "XV" / "123.srt", "1\n00:00:00,050 --> 00:00:00,150\nEdited\n\n");
        media::subtitles::install_pack(base / "game", base / "pack");
        require(media::subtitles::load_override(base / "game", "XV/123.xmv", movie_path)
                        ->front()
                        .text == L"Edited",
                "Edited local SRT was not loaded.");
        const auto second = base / "game" / "XV" / "456.xmv";
        fs::copy_file(movie_path, second);
        const std::string single = "1\n00:00:00,050 --> 00:00:00,150\nSecond clip\n\n";
        media::subtitles::install_text(base / "game", "XV/456.xmv", single);
        require(media::subtitles::load_override(base / "game", "XV/123.xmv", movie_path)
                        ->front()
                        .text == L"Edited",
                "Single-clip install replaced another movie's subtitles.");
        require(
            media::subtitles::load_override(base / "game", "XV/456.xmv", second)->front().text ==
                L"Second clip",
            "Single-clip subtitle was not installed.");
        rejected([&] { media::subtitles::install_text(base / "game", "XV/456.xmv", "invalid"); });
        require(
            media::subtitles::load_override(base / "game", "XV/456.xmv", second)->front().text ==
                L"Second clip",
            "Invalid single-clip edit changed the installation.");
        rejected([&] { media::subtitles::install_text(base / "game", "../456.xmv", single); });
        const auto zip = base / L"字幕.zip";
        const auto zipped = media::subtitles::export_zip(base / "game", zip);
        require(zipped.movies == 2 && zipped.cues == 2, "ZIP export lost current edits");
        media::subtitles::install_text(base / "game", "XV/456.xmv",
                                       "1\n00:00:00,050 --> 00:00:00,150\nTemporary\n");
        media::subtitles::install_zip(base / "game", zip);
        require(
            media::subtitles::load_override(base / "game", "XV/456.xmv", second)->front().text ==
                L"Second clip",
            "ZIP round trip lost edited subtitles");
        const auto original_zip = read(zip);
        rejected([&] {
            media::subtitles::export_zip(base / "game", zip, [](auto, auto) { return false; });
        });
        require(read(zip) == original_zip, "Cancelled ZIP export changed existing archive");
        const auto installed_srt = read(base / "game/subtitles/XV/456.srt");
        const auto installed_manifest = read(base / "game/subtitles/manifest.tsv");
        const auto bad_zip = base / "invalid.zip";
        const std::vector<std::pair<std::string, std::string>> valid_entries{
            {"manifest.tsv", installed_manifest},
            {"XV/123.srt", read(base / "game/subtitles/XV/123.srt")},
            {"XV/456.srt", installed_srt}};
        const auto with_extra = [&](const std::string& name, const std::string& content) {
            auto files = valid_entries;
            files.emplace_back(name, content);
            archive(bad_zip, files);
        };
        for (const auto* name : {"../escape.srt", "/absolute.srt", "C:/absolute.srt", "XN/CON.srt",
                                 "XN/../escape.srt", "XN//123.srt", "XN\\123.srt", "extra.exe"}) {
            with_extra(name, "bad");
            rejected([&] { media::subtitles::install_zip(base / "game", bad_zip); });
        }
        with_extra("xv/456.SRT", "duplicate");
        rejected([&] { media::subtitles::install_zip(base / "game", bad_zip); });
        archive(bad_zip, {{"XN/1.srt", "link"}}, 0120777u << 16);
        rejected([&] { media::subtitles::install_zip(base / "game", bad_zip); });
        with_extra("XN/1.srt", std::string(2 * 1024 * 1024 + 1, 'x'));
        rejected([&] { media::subtitles::install_zip(base / "game", bad_zip); });
        auto corrupt_crc = original_zip;
        const auto central = corrupt_crc.find("PK\x01\x02");
        require(central != std::string::npos, "Exported ZIP has no central directory");
        corrupt_crc[central + 16] ^= 1;
        write(bad_zip, corrupt_crc);
        rejected([&] { media::subtitles::install_zip(base / "game", bad_zip); });
        auto damaged = original_zip;
        damaged.resize(damaged.size() / 2);
        write(bad_zip, damaged);
        rejected([&] { media::subtitles::install_zip(base / "game", bad_zip); });
        rejected([&] {
            media::subtitles::install_zip(base / "game", zip, [](auto, auto) { return false; });
        });
        require(read(base / "game/subtitles/XV/456.srt") == installed_srt &&
                    read(base / "game/subtitles/manifest.tsv") == installed_manifest,
                "Failed ZIP import changed installed subtitles");
        archive(bad_zip, {{"Pack/manifest.tsv", installed_manifest},
                          {"Pack/XV/123.srt", read(base / "game/subtitles/XV/123.srt")},
                          {"Pack/XV/456.srt", installed_srt}});
        require(media::subtitles::install_zip(base / "game", bad_zip).movies == 2,
                "ZIP with one enclosing folder did not install");
        write(movie_path, Data{1, 2, 3});
        require(!media::subtitles::load_override(base / "game", "XV/123.xmv", movie_path),
                "Changed movie hash accepted a stale override.");
        if (argc == 3 && std::string_view(argv[1]) == "--archive-fixture") {
            fs::copy_file(zip, fs::path(argv[2]));
        }
        fs::remove_all(base);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        fs::remove_all(base);
        return 1;
    }
}
