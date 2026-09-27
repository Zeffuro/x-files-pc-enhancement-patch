#include "clip_defaults.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <stdexcept>

namespace devtools {
namespace {
std::wstring wide(std::string_view value) {
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (!value.empty() && !count) {
        throw std::runtime_error("Invalid clip label encoding.");
    }
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return result;
}
}

ClipDefaults parse_defaults(std::string_view text) {
    if (text.size() > 4 * 1024 * 1024 || text.find('\0') != text.npos) {
        throw std::runtime_error("Invalid clip label catalog.");
    }
    std::string normalized(text);
    normalized.erase(std::remove(normalized.begin(), normalized.end(), '\r'), normalized.end());
    text = normalized;
    if (text.starts_with("\xef\xbb\xbf")) {
        text.remove_prefix(3);
    }
    const bool places = text.starts_with("path\tlabel\tcomment\tplace\n");
    if (!places && !text.starts_with("path\tlabel\tcomment\n")) {
        throw std::runtime_error("Invalid clip label catalog.");
    }
    text.remove_prefix(text.find('\n') + 1);
    ClipDefaults result;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        text.remove_prefix(end == text.npos ? text.size() : end + 1);
        if (line.empty()) {
            continue;
        }
        const auto tab = line.find('\t'), last = line.find('\t', tab == line.npos ? 0 : tab + 1);
        const auto place_tab = line.find('\t', last == line.npos ? 0 : last + 1);
        if (tab == line.npos || last == line.npos ||
            (places ? (place_tab == line.npos || line.find('\t', place_tab + 1) != line.npos)
                    : place_tab != line.npos) ||
            line.size() > 18000) {
            throw std::runtime_error("Invalid clip label row.");
        }
        auto path = wide(line.substr(0, tab));
        if (path.empty() || path.size() > 240 || path.front() == L'/' ||
            path.find(L"..") != path.npos || path.find_first_of(L"\\:\r") != path.npos) {
            throw std::runtime_error("Invalid clip label path.");
        }
        std::transform(path.begin(), path.end(), path.begin(), std::towlower);
        auto label = wide(line.substr(tab + 1, last - tab - 1));
        auto comment = wide(line.substr(last + 1, places ? place_tab - last - 1 : line.npos));
        std::optional<std::wstring> place;
        if (places && place_tab + 1 < line.size()) {
            place = wide(line.substr(place_tab + 1));
            if (*place == L"-") {
                place->clear();
            }
        }
        if (label.empty() || label.size() > 1024 || result.size() >= 10000 ||
            !result.emplace(path, ClipDefault{{label, comment}, place}).second) {
            throw std::runtime_error("Duplicate or invalid clip label.");
        }
    }
    return result;
}

ClipDefaults load_defaults(const std::filesystem::path& root) {
    const auto path = root / L"tools" / L"clip-labels.tsv";
    if (std::filesystem::exists(path)) {
        const auto size = std::filesystem::file_size(path);
        if (size > 4 * 1024 * 1024) {
            throw std::runtime_error("Clip label catalog is too large.");
        }
        std::ifstream input(path, std::ios::binary);
        std::string text(static_cast<std::size_t>(size), '\0');
        if (!input.read(text.data(), static_cast<std::streamsize>(size))) {
            throw std::runtime_error("Cannot read clip label catalog.");
        }
        return parse_defaults(text);
    }
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(load_defaults), &module);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(2700), RT_RCDATA);
    if (!resource) {
        return {};
    }
    const auto data = LoadResource(module, resource);
    if (!data) {
        return {};
    }
    return parse_defaults(
        {static_cast<const char*>(LockResource(data)), SizeofResource(module, resource)});
}
}
