#pragma once

#include "media.h"
#include <map>
#include <string>
#include <string_view>

struct MediaRecord {
    std::uintmax_t size;
    std::string sha256;
};

std::map<std::wstring, MediaRecord> parse_catalog(std::string_view text);
std::map<std::wstring, MediaRecord> media_catalog(bool dvd);
