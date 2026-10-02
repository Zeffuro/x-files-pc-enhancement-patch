#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace game_assets {
struct DatabaseText {
    std::uint32_t offset;
    std::uint32_t size;
    std::string text;
};

class Database {
public:
    static Database load(const std::filesystem::path& path);
    static Database parse(std::span<const std::uint8_t> bytes);

    const std::array<std::uint32_t, 8>& header() const {
        return header_;
    }

    const std::vector<DatabaseText>& strings() const {
        return strings_;
    }

    std::size_t size() const {
        return bytes_.size();
    }

    std::span<const std::uint8_t> bytes() const {
        return bytes_;
    }

    std::wstring hex(std::size_t offset, std::size_t count = 256) const;

private:
    std::array<std::uint32_t, 8> header_{};
    std::vector<std::uint8_t> bytes_;
    std::vector<DatabaseText> strings_;
};
}
