#include "iso.h"
#include "catalog.h"
#include "disc_image.h"
#include "identity.h"

#include <windows.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c);
    });
    return text;
}

struct TemporaryCore {
    fs::path path;

    ~TemporaryCore() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
}

std::vector<MediaFile> read_iso(const fs::path& path) {
    return read_disc_image(path);
}

namespace {
std::string checksum(const MediaFile& file) {
    return media_file_sha256(file);
}

MediaSource prepare_image_import(const fs::path& root, const std::vector<MediaFile>& files) {
    MediaSource result;
    result.root = fs::canonical(root);
    result.set = detect_media_set(files);
    result.files = select_iso_files(files, media_catalog(result.set));
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
    if (!identity.edition ||
        identity.sha256 != media_catalog(result.set).at(L"xfiles.exe").sha256) {
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
        const auto name = media_relative(file.relative);
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
                                     ". Select a supported DVD ISO, or put all seven "
                                     "CD images of one language in one folder and use Folder.");
        }
        result.push_back(item->second);
    }
    return result;
}

MediaSource inspect_dvd_iso(const fs::path& image) {
    const auto path = fs::canonical(image);
    return prepare_image_import(path, read_iso(path));
}

MediaSource inspect_iso_folder(const fs::path& folder) {
    std::vector<fs::path> images;
    for (const auto& entry : fs::directory_iterator(folder)) {
        const auto extension = lower(entry.path().extension().wstring());
        if (entry.is_regular_file() && (extension == L".iso" || extension == L".cue")) {
            images.push_back(entry.path());
        }
    }
    if (images.empty()) {
        const bool mdf = std::any_of(
            fs::directory_iterator(folder), fs::directory_iterator{},
            [](const fs::directory_entry& entry) {
                const auto extension = lower(entry.path().extension().wstring());
                return entry.is_regular_file() && (extension == L".mdf" || extension == L".mds");
            });
        if (mdf) {
            throw std::runtime_error(
                "MDF/MDS images are not supported. Use ISO files or matching BIN/CUE pairs.");
        }
        throw std::runtime_error("No supported game found. Choose a mounted game folder, an ISO, "
                                 "or a folder containing all seven ISO or BIN/CUE CD images.");
    }
    std::sort(images.begin(), images.end());
    std::vector<MediaFile> files;
    const auto dvd_executable = media_catalog(true).at(L"xfiles.exe");
    fs::path dvd;
    for (const auto& image : images) {
        for (auto file : read_iso(image)) {
            if (media_relative(file.relative) == L"xfiles.exe" &&
                file.size == dvd_executable.size && checksum(file) == dvd_executable.sha256) {
                dvd = image;
            }
            files.push_back(std::move(file));
        }
    }
    if (!dvd.empty() && images.size() != 1) {
        throw std::runtime_error("This folder contains a DVD ISO and other images. Use Image to "
                                 "select the DVD, or choose only seven CD images of one format.");
    }
    return prepare_image_import(dvd.empty() ? folder : dvd, files);
}
