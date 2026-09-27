#include "subtitles.h"
#include <windows.h>
#include <objbase.h>
#include <cctype>
#include <ioapi.h>
#include <iowin32.h>
#include <unzip.h>
#include <zip.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>

namespace media::subtitles {
namespace {
namespace fs = std::filesystem;
constexpr std::uint64_t pack_limit = 128ull * 1024 * 1024;
constexpr std::size_t entry_limit = 2 * 1024 * 1024;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool unlinked(const fs::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

struct Workspace {
    fs::path path;

    explicit Workspace(const fs::path& parent) {
        require(unlinked(parent) && fs::is_directory(parent), "Invalid subtitle staging folder.");
        GUID guid{};
        require(SUCCEEDED(CoCreateGuid(&guid)), "Cannot create subtitle staging name.");
        wchar_t name[40]{};
        require(StringFromGUID2(guid, name, 40) != 0, "Cannot format subtitle staging name.");
        path = fs::absolute(parent) / (std::wstring(L".xfiles-subtitles-") + name);
        require(fs::create_directory(path), "Cannot create subtitle staging folder.");
    }

    ~Workspace() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

struct Reader {
    unzFile file = nullptr;

    ~Reader() {
        if (file) {
            unzClose(file);
        }
    }
};

struct Writer {
    zipFile file = nullptr;

    ~Writer() {
        if (file) {
            zipClose(file, nullptr);
        }
    }
};

std::string folded(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

std::string safe_name(std::string name, bool directory) {
    require(!name.empty() && name.size() <= 240, "Invalid ZIP entry name.");
    if (directory) {
        name.pop_back();
    }
    require(!name.empty() && std::all_of(name.begin(), name.end(),
                                         [](unsigned char c) {
                                             return (c >= 'a' && c <= 'z') ||
                                                    (c >= 'A' && c <= 'Z') ||
                                                    (c >= '0' && c <= '9') || c == '_' ||
                                                    c == '-' || c == '.' || c == '/';
                                         }),
            "Invalid subtitle ZIP path.");
    std::size_t offset = 0;
    while (offset < name.size()) {
        const auto end = name.find('/', offset);
        const auto part = name.substr(offset, end == name.npos ? end : end - offset);
        require(!part.empty() && part != "." && part != ".." && part.back() != '.',
                "Invalid subtitle ZIP path.");
        const auto stem = folded(part.substr(0, part.find('.')));
        require(stem != "con" && stem != "prn" && stem != "aux" && stem != "nul" &&
                    !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
                      stem.back() >= '1' && stem.back() <= '9'),
                "Reserved subtitle ZIP path.");
        if (end == name.npos) {
            break;
        }
        offset = end + 1;
    }
    require(name.back() != '/', "Invalid subtitle ZIP path.");
    return name;
}

void progress_tick(const Progress& progress, std::size_t done, std::size_t total) {
    require(!progress || progress(done, total), "Subtitle operation cancelled.");
}

void extract(const fs::path& archive, const fs::path& folder, const Progress& progress) {
    require(unlinked(archive) && fs::is_regular_file(archive) &&
                fs::file_size(archive) <= pack_limit,
            "Select a subtitle ZIP smaller than 128 MiB.");
    zlib_filefunc64_def io{};
    fill_win32_filefunc64W(&io);
    Reader reader{unzOpen2_64(archive.c_str(), &io)};
    require(reader.file != nullptr, "Cannot open subtitle ZIP.");
    unz_global_info64 global{};
    require(unzGetGlobalInfo64(reader.file, &global) == UNZ_OK && global.number_entry > 0 &&
                global.number_entry <= 20004,
            "Invalid subtitle ZIP directory.");
    require(unzGoToFirstFile(reader.file) == UNZ_OK, "Cannot read subtitle ZIP.");
    std::set<std::string> names;
    std::uint64_t total = 0;
    for (std::size_t index = 0; index < global.number_entry; ++index) {
        progress_tick(progress, index, static_cast<std::size_t>(global.number_entry));
        unz_file_info64 info{};
        std::array<char, 242> buffer{};
        require(unzGetCurrentFileInfo64(reader.file, &info, buffer.data(),
                                        static_cast<uLong>(buffer.size()), nullptr, 0, nullptr,
                                        0) == UNZ_OK &&
                    info.size_filename > 0 && info.size_filename <= 240,
                "Invalid subtitle ZIP entry.");
        const std::string original(buffer.data(), info.size_filename);
        const bool directory = original.back() == '/';
        const auto name = safe_name(original, directory);
        const auto key = folded(name);
        const auto mode = (info.external_fa >> 16) & 0170000;
        require(!(info.flag & 1) && !(info.external_fa & FILE_ATTRIBUTE_REPARSE_POINT) &&
                    (mode == 0 || mode == (directory ? 0040000u : 0100000u)) &&
                    (info.compression_method == 0 || info.compression_method == Z_DEFLATED) &&
                    info.uncompressed_size <= entry_limit &&
                    info.uncompressed_size <= pack_limit - total && names.insert(key).second,
                "Subtitle ZIP contains a linked, duplicate, encrypted or oversized entry.");
        total += info.uncompressed_size;
        const auto target = folder / name;
        if (directory) {
            require(info.uncompressed_size == 0, "Invalid subtitle ZIP folder.");
            fs::create_directories(target);
        } else {
            require(key == "manifest.tsv" || key.ends_with("/manifest.tsv") ||
                        key == "skipped.txt" || key.ends_with("/skipped.txt") ||
                        key.ends_with(".srt"),
                    "Subtitle ZIP must contain only the manifest, SRT files and skipped.txt.");
            fs::create_directories(target.parent_path());
            require(unzOpenCurrentFile(reader.file) == UNZ_OK, "Cannot read subtitle ZIP entry.");
            std::string bytes(static_cast<std::size_t>(info.uncompressed_size), '\0');
            std::size_t offset = 0;
            while (offset < bytes.size()) {
                const auto count = unzReadCurrentFile(reader.file, bytes.data() + offset,
                                                      static_cast<unsigned>(bytes.size() - offset));
                require(count > 0, "Truncated subtitle ZIP entry.");
                offset += count;
            }
            char extra{};
            require(unzReadCurrentFile(reader.file, &extra, 1) == 0,
                    "Incorrect subtitle ZIP entry size.");
            require(unzCloseCurrentFile(reader.file) == UNZ_OK, "Subtitle ZIP checksum failed.");
            std::ofstream output(target, std::ios::binary);
            require(bool(output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))),
                    "Cannot extract subtitle ZIP.");
        }
        if (index + 1 < global.number_entry) {
            require(unzGoToNextFile(reader.file) == UNZ_OK, "Truncated subtitle ZIP directory.");
        }
    }
}

void compress(const fs::path& folder, const fs::path& output, const Progress& progress) {
    zlib_filefunc64_def io{};
    fill_win32_filefunc64W(&io);
    Writer writer{zipOpen2_64(output.c_str(), APPEND_STATUS_CREATE, nullptr, &io)};
    require(writer.file != nullptr, "Cannot create subtitle ZIP.");
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    std::uint64_t total = 0;
    for (std::size_t index = 0; index < files.size(); ++index) {
        progress_tick(progress, index, files.size());
        const auto size = fs::file_size(files[index]);
        require(size <= entry_limit && size <= pack_limit - total, "Subtitle pack is too large.");
        total += size;
        std::ifstream input(files[index], std::ios::binary);
        std::string bytes(static_cast<std::size_t>(size), '\0');
        require(bool(input.read(bytes.data(), static_cast<std::streamsize>(size))),
                "Cannot read exported subtitles.");
        const auto name = files[index].lexically_relative(folder).generic_string();
        zip_fileinfo info{};
        info.tmz_date.tm_year = 1980;
        info.tmz_date.tm_mday = 1;
        require(zipOpenNewFileInZip64(writer.file, name.c_str(), &info, nullptr, 0, nullptr, 0,
                                      nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0) == ZIP_OK &&
                    zipWriteInFileInZip(writer.file, bytes.data(),
                                        static_cast<unsigned>(bytes.size())) == ZIP_OK &&
                    zipCloseFileInZip(writer.file) == ZIP_OK,
                "Cannot write subtitle ZIP.");
    }
    const auto file = writer.file;
    writer.file = nullptr;
    require(zipClose(file, nullptr) == ZIP_OK, "Cannot finish subtitle ZIP.");
    progress_tick(progress, files.size(), files.size());
}
}

Result export_zip(const fs::path& game_root, const fs::path& output_zip, const Progress& progress) {
    const auto output = fs::absolute(output_zip);
    require(!fs::exists(output) || (unlinked(output) && fs::is_regular_file(output)),
            "Cannot replace the selected subtitle ZIP.");
    Workspace workspace(output.parent_path());
    const auto result = export_pack(game_root, workspace.path / "pack", progress);
    const auto archive = workspace.path / "subtitles.zip";
    compress(workspace.path / "pack", archive, progress);
    require(MoveFileExW(archive.c_str(), output.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
            "Cannot save subtitle ZIP.");
    return result;
}

Result install_zip(const fs::path& game_root, const fs::path& input_zip, const Progress& progress) {
    Workspace workspace(fs::temp_directory_path());
    extract(input_zip, workspace.path, progress);
    auto folder = workspace.path;
    if (!fs::is_regular_file(folder / "manifest.tsv")) {
        const auto first = fs::directory_iterator(folder);
        require(first != fs::directory_iterator() && first->is_directory(),
                "Subtitle ZIP has no manifest.tsv at its root.");
        folder = first->path();
        auto next = first;
        require(++next == fs::directory_iterator(),
                "Put manifest.tsv and its SRT folders together inside the ZIP.");
    }
    return install_pack(game_root, folder, progress);
}
}
