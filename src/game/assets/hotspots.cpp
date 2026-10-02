#include "hotspots.h"
#include <algorithm>
#include <fstream>

namespace game_assets {
namespace {
std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

std::int16_t coordinate(std::span<const std::uint8_t> bytes, std::size_t offset) {
    const auto value = std::int32_t(bytes[offset]) | (std::int32_t(bytes[offset + 1]) << 8);
    return static_cast<std::int16_t>(value >= 0x8000 ? value - 0x10000 : value);
}
}

bool HotspotEntry::ordered() const {
    return x_min <= x_max && y_min <= y_max;
}

bool HotspotEntry::on_canvas() const {
    return ordered() && x_min >= 0 && y_min >= 0 && x_max <= 640 && y_max <= 480;
}

HotspotFile parse_hotspots(std::span<const std::uint8_t> bytes) {
    HotspotFile result;
    result.file_size = bytes.size();
    const auto raw = bytes.first(std::min(bytes.size(), hotspot_raw_limit));
    result.raw.assign(raw.begin(), raw.end());
    if (bytes.size() < 8) {
        result.status = L"Hotspot header is incomplete";
        return result;
    }
    if (bytes[0] != 'H' || bytes[1] != 'S' || bytes[2] != 'P' || bytes[3] != 'T') {
        result.status = L"Hotspot magic is not HSPT";
        return result;
    }
    const auto count = word(bytes, 4);
    if (count > hotspot_entry_limit) {
        result.status = L"Hotspot count exceeds the 4096-entry safety limit";
        return result;
    }
    if (bytes.size() != 8 + std::size_t(count) * 20) {
        result.status = L"Hotspot size does not match 8 + count * 20";
        return result;
    }
    result.entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = 8 + index * 20;
        result.entries.push_back({word(bytes, offset), coordinate(bytes, offset + 4),
                                  coordinate(bytes, offset + 6), coordinate(bytes, offset + 8),
                                  coordinate(bytes, offset + 10), word(bytes, offset + 12),
                                  word(bytes, offset + 16)});
    }
    result.valid = true;
    result.status = L"HSPT geometry decoded";
    return result;
}

HotspotFile load_hotspots(const std::filesystem::path& path) {
    HotspotFile result;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size < std::streampos(0)) {
        result.status = L"Hotspot file could not be read";
        return result;
    }
    result.file_size = static_cast<std::uint64_t>(size);
    constexpr std::size_t maximum = 8 + hotspot_entry_limit * 20;
    const bool oversized = result.file_size > maximum;
    const auto read_size = oversized ? hotspot_raw_limit : std::size_t(result.file_size);
    std::vector<std::uint8_t> bytes(read_size);
    file.seekg(0);
    if (read_size && !file.read(reinterpret_cast<char*>(bytes.data()),
                                static_cast<std::streamsize>(read_size))) {
        result.status = L"Hotspot file changed or could not be read completely";
        return result;
    }
    if (oversized) {
        result.raw = std::move(bytes);
        result.status = L"Hotspot file exceeds the 81928-byte safety limit";
        return result;
    }
    if (file.peek() != std::char_traits<char>::eof()) {
        result.status = L"Hotspot file changed while being read";
        return result;
    }
    return parse_hotspots(bytes);
}
}
