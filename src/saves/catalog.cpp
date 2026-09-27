#include "catalog.h"
#include "header.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <stdexcept>

namespace saves {
namespace {
bool ordinary(const std::filesystem::path& path, bool directory) {
    const auto flags = GetFileAttributesW(path.c_str());
    return flags != INVALID_FILE_ATTRIBUTES && !(flags & FILE_ATTRIBUTE_REPARSE_POINT) &&
           bool(flags & FILE_ATTRIBUTE_DIRECTORY) == directory;
}

std::wstring date(std::filesystem::file_time_type time) {
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    const auto seconds = std::chrono::system_clock::to_time_t(system);
    std::tm local{};
    if (localtime_s(&local, &seconds)) {
        return {};
    }
    std::array<wchar_t, 32> text{};
    std::wcsftime(text.data(), text.size(), L"%Y-%m-%d %H:%M", &local);
    return text.data();
}
}

Catalog read_catalog(const std::filesystem::path& game) {
    const auto directory = game / L"saves";
    Catalog result;
    if (!std::filesystem::exists(directory)) {
        return result;
    }
    if (!ordinary(directory, true)) {
        throw std::runtime_error("The saves folder must be a regular folder.");
    }
    unsigned visited = 0;
    for (const auto& file : std::filesystem::directory_iterator(directory)) {
        if (++visited > 10000) {
            result.truncated = true;
            break;
        }
        if (_wcsicmp(file.path().extension().c_str(), L".x")) {
            continue;
        }
        if (!ordinary(file.path(), false)) {
            ++result.skipped;
            continue;
        }
        try {
            Entry entry;
            entry.path = file.path();
            entry.name = file.path().stem().wstring();
            entry.timestamp = file.last_write_time();
            entry.modified = date(entry.timestamp);
            entry.bytes = file.file_size();
            entry.header_supported = supported_header(entry.path);
            result.entries.push_back(std::move(entry));
        } catch (const std::filesystem::filesystem_error&) {
            ++result.skipped;
        }
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const Entry& a, const Entry& b) {
        const auto compared = _wcsicmp(a.name.c_str(), b.name.c_str());
        return compared ? compared < 0 : a.path.native() < b.path.native();
    });
    return result;
}

std::vector<Entry> page(const Catalog& catalog, std::size_t index, std::size_t page_size) {
    if (!page_size || page_size > 100 || index > catalog.entries.size() / page_size) {
        return {};
    }
    const auto begin = index * page_size;
    const auto end = begin + std::min(page_size, catalog.entries.size() - begin);
    return {catalog.entries.begin() + begin, catalog.entries.begin() + end};
}
}
