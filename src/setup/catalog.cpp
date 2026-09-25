#include "catalog.h"

#include <windows.h>
#include <algorithm>
#include <sstream>
#include <stdexcept>

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
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(dvd ? 1202 : 1201), RT_RCDATA);
    const auto loaded = resource ? LoadResource(module, resource) : nullptr;
    const auto data = loaded ? LockResource(loaded) : nullptr;
    const auto size = resource ? SizeofResource(module, resource) : 0;
    if (!data || !size) {
        throw std::runtime_error("Setup is missing its media catalog.");
    }
    return parse_catalog({static_cast<const char*>(data), size});
}
