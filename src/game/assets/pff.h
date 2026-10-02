#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace game_assets {
struct PffEntry {
    std::uint32_t offset = 0, size = 0, header_value = 0;
};

inline constexpr std::size_t pff_file_limit = 512 * 1024 * 1024;
inline constexpr std::size_t pff_entry_limit = 65536;

class PffArchive {
public:
    static std::optional<PffArchive> parse(std::span<const std::uint8_t> bytes,
                                           std::wstring* error = nullptr);
    static std::optional<PffArchive> load(const std::filesystem::path& path,
                                          std::wstring* error = nullptr);

    std::span<const PffEntry> entries() const;
    std::span<const std::uint8_t> entry(std::size_t index) const;
    std::span<const std::uint8_t> bytes() const;
    std::optional<std::vector<std::uint8_t>> replace(std::size_t index,
                                                     std::span<const std::uint8_t> payload,
                                                     std::wstring* error = nullptr) const;

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<PffEntry> entries_;
};
}
