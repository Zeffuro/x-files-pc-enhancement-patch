#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace saves {
struct Entry {
    std::filesystem::path path;
    std::wstring name;
    std::wstring modified;
    std::filesystem::file_time_type timestamp{};
    std::uintmax_t bytes = 0;
    bool header_supported = false;
};

struct Catalog {
    std::vector<Entry> entries;
    unsigned skipped = 0;
    bool truncated = false;
};

// Read-only discovery. The native loader must still validate a selected save.
Catalog read_catalog(const std::filesystem::path& game);
std::vector<Entry> page(const Catalog& catalog, std::size_t index, std::size_t page_size);
}
