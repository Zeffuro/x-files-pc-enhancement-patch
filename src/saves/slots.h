#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace saves {
inline constexpr unsigned slots_per_page = 6;
inline constexpr unsigned slot_pages = 100;

struct Thumbnail {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};

struct Slot {
    unsigned number = 0;
    std::wstring name, date;
    std::filesystem::path file, thumbnail;
    bool occupied = false, readable = false;
};

Slot read_slot(const std::filesystem::path& game, unsigned number);
Thumbnail read_thumbnail(const std::filesystem::path& path);
void delete_slot(const std::filesystem::path& game, unsigned number);
// Publish metadata only after the new save and thumbnail have been written.
void write_slot(const std::filesystem::path& game, unsigned number, const std::wstring& name,
                const std::filesystem::path& prepared_save, const Thumbnail& thumbnail);
}
