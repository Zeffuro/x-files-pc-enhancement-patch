#pragma once

#include "media.h"
#include <map>
#include <string>
#include <string_view>
#include <span>
#include <set>

struct MediaSet {
    MediaSetId id;
    const char* name;
    unsigned resource;
    bool dvd;
};

struct MediaRecord {
    std::uintmax_t size;
    std::string sha256;
};

std::map<std::wstring, MediaRecord> parse_catalog(std::string_view text);
std::map<std::wstring, MediaRecord> media_catalog(bool dvd);
std::map<std::wstring, MediaRecord> media_catalog(MediaSetId set);
std::span<const MediaSet> media_sets();
MediaSetId detect_media_set(const std::vector<MediaFile>& files);
MediaSetId identify_media_set(const std::map<std::wstring, std::set<std::string>>& hashes);
std::wstring media_relative(const std::filesystem::path& path);
