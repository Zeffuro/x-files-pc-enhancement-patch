#include "game/assets/hotspots.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value,
         std::size_t width = 4) {
    for (std::size_t at = 0; at < width; ++at) {
        bytes[offset + at] = static_cast<std::uint8_t>(value >> (at * 8));
    }
}

std::vector<std::uint8_t> fixture(std::size_t count) {
    std::vector<std::uint8_t> bytes(8 + count * 20);
    std::copy_n("HSPT", 4, bytes.begin());
    put(bytes, 4, static_cast<std::uint32_t>(count));
    return bytes;
}

struct TempFile {
    std::filesystem::path root, path;

    TempFile() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root =
            std::filesystem::temp_directory_path() / ("xfiles-hotspots-" + std::to_string(stamp));
        require(std::filesystem::create_directory(root), "Temporary fixture creation failed");
        path = root / "test.HOT";
    }

    void write(std::span<const std::uint8_t> bytes) const {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        require(bool(file), "Fixture write failed");
    }

    ~TempFile() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

void parser_checks() {
    using namespace game_assets;
    require(parse_hotspots(fixture(0)).valid, "Empty HOT was rejected");
    auto bytes = fixture(2);
    put(bytes, 8, 0x87654321);
    put(bytes, 12, 0xffff, 2);
    put(bytes, 14, 0x8000, 2);
    put(bytes, 16, 640, 2);
    put(bytes, 18, 480, 2);
    put(bytes, 20, 0x12345678);
    put(bytes, 24, 0xfedcba98);
    put(bytes, 36, 640, 2);
    put(bytes, 38, 480, 2);
    auto result = parse_hotspots(bytes);
    require(result.valid && result.entries.size() == 2 && result.raw == bytes,
            "Valid HOT coverage or raw bytes failed");
    const auto& first = result.entries[0];
    require(first.type_zorder == 0x87654321 && first.x_min == -1 && first.y_min == -32768 &&
                first.x_max == 640 && first.y_max == 480 && first.action_id_1 == 0x12345678 &&
                first.action_id_2 == 0xfedcba98,
            "Little-endian or signed coordinate decoding failed");
    require(first.ordered() && !first.on_canvas() && result.entries[1].on_canvas(),
            "Canvas boundary classification failed");
    put(bytes, 12, 641, 2);
    require(!parse_hotspots(bytes).entries[0].ordered(), "Reversed rectangle was not flagged");
    put(bytes, 12, 0, 2);
    put(bytes, 14, 0, 2);
    put(bytes, 16, 641, 2);
    put(bytes, 18, 481, 2);
    result = parse_hotspots(bytes);
    require(result.valid && !result.entries[0].on_canvas(),
            "Out-of-canvas raw coordinates were rejected or not flagged");
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        result = parse_hotspots(std::span(bytes).first(length));
        require(!result.valid && result.entries.empty(), "Truncated HOT produced partial entries");
    }
    bytes.push_back(0);
    require(!parse_hotspots(bytes).valid, "Trailing bytes accepted");
    bytes = fixture(1);
    bytes[0] = 'h';
    require(!parse_hotspots(bytes).valid, "Wrong magic accepted");
    bytes[0] = 'H';
    put(bytes, 4, 0xffffffff);
    require(!parse_hotspots(bytes).valid, "Overflowing count accepted");
    put(bytes, 4, 0x01000000);
    require(!parse_hotspots(bytes).valid, "Big-endian count accepted");
    put(bytes, 4, 2);
    require(!parse_hotspots(bytes).valid, "Mismatched count accepted");
    bytes = fixture(hotspot_entry_limit);
    result = parse_hotspots(bytes);
    require(result.valid && result.entries.size() == hotspot_entry_limit &&
                result.raw.size() == hotspot_raw_limit && result.file_size == bytes.size(),
            "Count or raw-byte boundary failed");
    bytes = fixture(hotspot_entry_limit + 1);
    result = parse_hotspots(bytes);
    require(!result.valid && result.entries.empty() && result.raw.size() == hotspot_raw_limit,
            "Count safety limit failed");
    TempFile file;
    require(!load_hotspots(file.path).valid, "Missing file accepted");
    file.write(fixture(1));
    require(load_hotspots(file.path).valid, "Valid file loader failed");
    file.write(bytes);
    result = load_hotspots(file.path);
    require(!result.valid && result.entries.empty() && result.raw.size() == hotspot_raw_limit &&
                result.file_size == bytes.size(),
            "File size safety limit failed");
    file.write(std::span(bytes).first(7));
    require(!load_hotspots(file.path).valid, "Truncated file loader accepted header");
}

void inventory(const std::filesystem::path& root) {
    std::size_t files = 0, entries = 0, max_count = 0;
    for (const auto& file : std::filesystem::recursive_directory_iterator(root)) {
        auto extension = file.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (!file.is_regular_file() || extension != ".hot") {
            continue;
        }
        const auto result = game_assets::load_hotspots(file.path());
        require(result.valid, "Installed HOT failed parser validation");
        for (const auto& entry : result.entries) {
            require(entry.on_canvas(), "Installed HOT has unexpected coordinate bounds");
        }
        ++files;
        entries += result.entries.size();
        max_count = std::max(max_count, result.entries.size());
    }
    require(files == 680 && entries == 2279 && max_count == 46, "Installed HOT inventory changed");
    std::cout << root << ": " << files << " HOT files, " << entries << " entries, max " << max_count
              << '\n';
}
}

int main(int argc, char** argv) {
    try {
        parser_checks();
        for (int at = 1; at < argc; ++at) {
            inventory(argv[at]);
        }
    } catch (const std::exception& error) {
        std::cerr << "Hotspot test failed: " << error.what() << '\n';
        return 1;
    }
}
