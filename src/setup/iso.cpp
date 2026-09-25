#include "iso.h"
#include "catalog.h"
#include "identity.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
constexpr std::uint64_t sector = 2048;
using Bytes = std::vector<unsigned char>;

unsigned little(const unsigned char* p) {
    return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16 | unsigned(p[3]) << 24;
}

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c);
    });
    return text;
}

class Image {
    fs::path path;
    std::ifstream stream;
    std::uint64_t size;
    std::set<std::uint64_t> directories;
    std::vector<MediaFile> files;

    Bytes read(std::uint64_t offset, std::uint64_t count) {
        if (offset > size || count > size - offset || count > 16 * 1024 * 1024) {
            throw std::runtime_error("Invalid ISO directory extent.");
        }
        Bytes bytes(static_cast<std::size_t>(count));
        stream.seekg(static_cast<std::streamoff>(offset));
        if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
            throw std::runtime_error("Cannot read ISO directory.");
        }
        return bytes;
    }

    void directory(std::uint64_t offset, unsigned length, const fs::path& parent, unsigned depth) {
        if (depth > 16 || directories.size() > 4096 || !directories.insert(offset).second) {
            throw std::runtime_error("Invalid or recursive ISO directory.");
        }
        const auto data = read(offset, length);
        for (std::size_t i = 0; i < data.size();) {
            const auto record_size = data[i];
            if (!record_size) {
                i = (i / sector + 1) * sector;
                continue;
            }
            if (record_size < 34 || record_size > data.size() - i ||
                i % sector + record_size > sector) {
                throw std::runtime_error("Truncated ISO directory record.");
            }
            const auto p = data.data() + i;
            i += record_size;
            const unsigned name_size = p[32];
            if (!name_size || 33 + name_size > record_size) {
                throw std::runtime_error("Invalid ISO filename.");
            }
            if (name_size == 1 && p[33] <= 1) {
                continue;
            }
            if (p[1] || p[26] || p[27] || (p[25] & 0x80)) {
                throw std::runtime_error(
                    "This ISO layout is not supported. Mount the disc instead.");
            }
            std::wstring name;
            for (unsigned j = 0; j < name_size && p[33 + j] != ';'; ++j) {
                const auto c = p[33 + j];
                if (c < 32 || c >= 127 || c == '/' || c == '\\' || c == ':') {
                    throw std::runtime_error("Unsafe ISO filename.");
                }
                name += c;
            }
            if (name.empty() || name == L"." || name == L"..") {
                throw std::runtime_error("Invalid ISO filename.");
            }
            const auto relative = parent / name;
            const auto start = std::uint64_t(little(p + 2)) * sector;
            const auto count = little(p + 10);
            if (start > size || count > size - start) {
                throw std::runtime_error("ISO file extends beyond the image.");
            }
            if (p[25] & 2) {
                directory(start, count, relative, depth + 1);
            } else {
                if (files.size() >= 100000) {
                    throw std::runtime_error("Too many ISO files.");
                }
                files.push_back({path, relative, count, {}, start});
            }
        }
    }

public:
    explicit Image(const fs::path& source)
        : path(source), stream(source, std::ios::binary), size(fs::file_size(source)) {}

    std::vector<MediaFile> list() {
        for (unsigned block = 16; block < 64; ++block) {
            const auto data = read(block * sector, sector);
            if (std::string(data.begin() + 1, data.begin() + 6) != "CD001" || data[6] != 1) {
                break;
            }
            if (data[0] == 1) {
                if (data[128] != 0 || data[129] != 8 || data[156] < 34) {
                    break;
                }
                directory(std::uint64_t(little(data.data() + 158)) * sector,
                          little(data.data() + 166), {}, 0);
                return std::move(files);
            }
            if (data[0] == 255) {
                break;
            }
        }
        throw std::runtime_error(
            "No supported ISO9660 filesystem. For a UDF-only DVD image, "
            "mount it in Windows, then use Folder to select the mounted disc.");
    }
};

struct TemporaryCore {
    fs::path path;

    ~TemporaryCore() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
}

std::vector<MediaFile> read_iso(const fs::path& path) {
    return Image(path).list();
}

void copy_media_file(const MediaFile& file, const fs::path& output,
                     const std::function<bool()>& keep_going) {
    if (!file.offset) {
        fs::copy_file(file.source, output, fs::copy_options::overwrite_existing);
        return;
    }
    std::ifstream input(file.source, std::ios::binary);
    std::ofstream destination(output, std::ios::binary);
    input.seekg(static_cast<std::streamoff>(*file.offset));
    std::vector<char> buffer(1024 * 1024);
    auto remaining = file.size;
    while (remaining) {
        if (!keep_going()) {
            throw std::runtime_error("Setup cancelled; your source files were not changed.");
        }
        const auto count =
            static_cast<std::streamsize>(std::min<std::uintmax_t>(remaining, buffer.size()));
        if (!input.read(buffer.data(), count) || !destination.write(buffer.data(), count)) {
            throw std::runtime_error(
                "Cannot copy a file from the ISO. Check the image and free space.");
        }
        remaining -= count;
    }
    destination.close();
    if (!destination) {
        throw std::runtime_error("Cannot finish writing the imported file.");
    }
}

namespace {
std::wstring game_path(const fs::path& path) {
    auto name = lower(path.generic_wstring());
    if (name.starts_with(L"english/")) {
        name.erase(0, 8);
    }
    if (name.starts_with(L"mininst/") || name.starts_with(L"medinst/")) {
        name.erase(0, 8);
    }
    return name;
}

std::string checksum(const MediaFile& file) {
    return file.offset ? sha256(file.source, *file.offset, file.size) : sha256(file.source);
}

MediaSource prepare_image_import(const fs::path& root, const std::vector<MediaFile>& files,
                                 bool dvd) {
    MediaSource result;
    result.root = fs::canonical(root);
    result.files = select_iso_files(files, media_catalog(dvd));
    for (const auto& file : result.files) {
        result.bytes += file.size;
    }
    const auto core =
        fs::temp_directory_path() / (L"xfiles-disc-core-" + std::to_wstring(GetCurrentProcessId()) +
                                     L"-" + std::to_wstring(GetTickCount64()));
    auto lifetime = std::make_shared<TemporaryCore>();
    if (!fs::create_directory(core)) {
        throw std::runtime_error("Cannot create the disc import workspace.");
    }
    lifetime->path = core;
    for (const auto& file : result.files) {
        if (file.relative.has_parent_path()) {
            continue;
        }
        copy_media_file(file, core / file.relative, [] { return true; });
        if (sha256(core / file.relative) != file.checksum) {
            throw std::runtime_error("The disc image contains a damaged or unsupported game file.");
        }
    }
    const auto identity = identify(core / L"XFiles.exe");
    if (!identity.edition || std::string(identity.edition) != (dvd ? "DVD" : "CD")) {
        throw std::runtime_error("Unsupported game executable in the disc image(s).");
    }
    result.game = core;
    result.workspace = std::move(lifetime);
    return result;
}
}

std::vector<MediaFile> select_iso_files(const std::vector<MediaFile>& files,
                                        const std::map<std::wstring, MediaRecord>& catalog) {
    std::map<std::wstring, MediaFile> found;
    std::set<std::wstring> verified;
    for (auto file : files) {
        const auto name = game_path(file.relative);
        const auto record = catalog.find(name);
        if (record == catalog.end() || record->second.size != file.size) {
            continue;
        }
        file.relative = name;
        file.checksum = record->second.sha256;
        const auto previous = found.find(name);
        if (previous == found.end()) {
            found.emplace(name, std::move(file));
        } else if (!verified.contains(name)) {
            if (checksum(previous->second) == file.checksum) {
                verified.insert(name);
            } else if (checksum(file) == file.checksum) {
                previous->second = std::move(file);
                verified.insert(name);
            }
        }
    }
    std::vector<MediaFile> result;
    for (const auto& [name, record] : catalog) {
        const auto item = found.find(name);
        if (item == found.end()) {
            throw std::runtime_error("The disc images are incomplete or unsupported: missing " +
                                     fs::path(name).string() +
                                     ". Select an English PC DVD ISO, or put all seven "
                                     "English PC CD ISOs in one folder and use Folder.");
        }
        result.push_back(item->second);
    }
    return result;
}

MediaSource inspect_dvd_iso(const fs::path& image) {
    const auto path = fs::canonical(image);
    return prepare_image_import(path, read_iso(path), true);
}

MediaSource inspect_iso_folder(const fs::path& folder) {
    std::vector<fs::path> images;
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file() && lower(entry.path().extension().wstring()) == L".iso") {
            images.push_back(entry.path());
        }
    }
    if (images.empty()) {
        throw std::runtime_error("No supported game found. Choose an English PC DVD folder or "
                                 "ISO, a complete CD folder, or a folder with all seven CD ISOs.");
    }
    std::sort(images.begin(), images.end());
    std::vector<MediaFile> files;
    const auto dvd_executable = media_catalog(true).at(L"xfiles.exe");
    fs::path dvd;
    for (const auto& image : images) {
        for (auto file : read_iso(image)) {
            if (game_path(file.relative) == L"xfiles.exe" && file.size == dvd_executable.size &&
                checksum(file) == dvd_executable.sha256) {
                dvd = image;
            }
            files.push_back(std::move(file));
        }
    }
    if (!dvd.empty() && images.size() != 1) {
        throw std::runtime_error("This folder contains a DVD ISO and other images. Use ISO to "
                                 "select the DVD, or choose a folder with only the seven CD ISOs.");
    }
    return prepare_image_import(dvd.empty() ? folder : dvd, files, !dvd.empty());
}
