#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace game_assets {
struct HotspotEntry {
    std::uint32_t type_zorder = 0;
    std::int16_t x_min = 0, y_min = 0, x_max = 0, y_max = 0;
    std::uint32_t action_id_1 = 0, action_id_2 = 0;

    bool ordered() const;
    bool on_canvas() const;
};

struct HotspotFile {
    bool valid = false;
    std::wstring status;
    std::uint64_t file_size = 0;
    std::vector<HotspotEntry> entries;
    std::vector<std::uint8_t> raw;
};

inline constexpr std::size_t hotspot_entry_limit = 4096;
inline constexpr std::size_t hotspot_raw_limit = 4096;
HotspotFile parse_hotspots(std::span<const std::uint8_t> bytes);
HotspotFile load_hotspots(const std::filesystem::path& path);
}
