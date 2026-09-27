#include "catalog.h"
#include "disc_image.h"

#include <windows.h>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <set>

namespace {
constexpr MediaSet sets[] = {
    {MediaSetId::cd_en, "English CD", 1201, false},
    {MediaSetId::dvd_en, "English DVD", 1202, true},
    {MediaSetId::cd_de, "German CD", 1203, false},
    {MediaSetId::cd_fr, "French CD", 1204, false},
    {MediaSetId::cd_es, "Spanish CD", 1205, false},
    {MediaSetId::cd_it, "Italian CD", 1206, false},
    {MediaSetId::cd_jp, "Japanese CD", 1207, false},
};
constexpr const wchar_t* identity_files[] = {L"xfiles.exe",  L"xfiles.hdb",  L"xfiles.gam",
                                             L"xfilesc.dll", L"xfilese.dll", L"xfiless.dll",
                                             L"xfilest.dll"};
}

std::span<const MediaSet> media_sets() {
    return sets;
}

std::map<std::wstring, MediaRecord> parse_catalog(std::string_view text) {
    std::map<std::wstring, MediaRecord> records;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string name, hash, extra;
        std::uintmax_t size = 0;
        if (!(row >> name >> size >> hash) || row >> extra || hash.size() != 64 ||
            !std::all_of(hash.begin(), hash.end(),
                         [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
            name.empty() || name.front() == '/' || name.find("..") != std::string::npos ||
            !std::all_of(name.begin(), name.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '/' ||
                       c == '_';
            })) {
            throw std::runtime_error("The setup media catalog is invalid.");
        }
        if (!records.emplace(std::wstring(name.begin(), name.end()), MediaRecord{size, hash})
                 .second) {
            throw std::runtime_error("The setup media catalog contains duplicate entries.");
        }
    }
    if (records.empty()) {
        throw std::runtime_error("The setup media catalog is empty.");
    }
    return records;
}

std::map<std::wstring, MediaRecord> media_catalog(bool dvd) {
    return media_catalog(dvd ? MediaSetId::dvd_en : MediaSetId::cd_en);
}

std::map<std::wstring, MediaRecord> media_catalog(MediaSetId set) {
    const auto entry = std::find_if(std::begin(sets), std::end(sets),
                                    [&](const MediaSet& value) { return value.id == set; });
    if (entry == std::end(sets)) {
        throw std::runtime_error("Unknown game media set.");
    }
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(entry->resource), RT_RCDATA);
    const auto loaded = resource ? LoadResource(module, resource) : nullptr;
    const auto data = loaded ? LockResource(loaded) : nullptr;
    const auto size = resource ? SizeofResource(module, resource) : 0;
    if (!data || !size) {
        throw std::runtime_error("Setup is missing its media catalog.");
    }
    return parse_catalog({static_cast<const char*>(data), size});
}

std::wstring media_relative(const std::filesystem::path& path) {
    auto name = path.generic_wstring();
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c);
    });
    if (name.starts_with(L"english/")) {
        name.erase(0, 8);
    }
    if (name.starts_with(L"mininst/") || name.starts_with(L"medinst/")) {
        name.erase(0, 8);
    }
    return name;
}

MediaSetId detect_media_set(const std::vector<MediaFile>& files) {
    std::map<std::wstring, std::set<std::string>> hashes;
    for (const auto& file : files) {
        const auto name = media_relative(file.relative);
        if (std::find(std::begin(identity_files), std::end(identity_files), name) !=
            std::end(identity_files)) {
            hashes[name].insert(media_file_sha256(file));
        }
    }
    return identify_media_set(hashes);
}

MediaSetId identify_media_set(const std::map<std::wstring, std::set<std::string>>& hashes) {
    MediaSetId detected = MediaSetId::unknown;
    for (const auto& set : sets) {
        const auto catalog = media_catalog(set.id);
        const bool matches = std::all_of(
            std::begin(identity_files), std::end(identity_files), [&](const auto* name) {
                const auto found = hashes.find(name);
                return found != hashes.end() && found->second.contains(catalog.at(name).sha256);
            });
        if (matches) {
            if (detected != MediaSetId::unknown) {
                throw std::runtime_error(
                    "Multiple game media sets found. Select one language and release.");
            }
            detected = set.id;
        }
    }
    if (detected == MediaSetId::unknown) {
        throw std::runtime_error(
            "Unsupported or mixed game media. Select a complete English CD/DVD "
            "or German, French, Spanish, Italian or Japanese CD set.");
    }
    return detected;
}
