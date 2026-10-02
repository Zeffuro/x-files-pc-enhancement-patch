#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace game_assets {
struct ResourceString {
    std::uint32_t id = 0;
    std::uint32_t language = 0;
    std::uint32_t code_page = 0;
    std::size_t file_offset = 0;
    std::wstring text;
};

struct ResourceStrings {
    bool valid = false;
    std::size_t file_size = 0;
    std::vector<ResourceString> strings;
    std::wstring status;
};

inline constexpr std::size_t resource_file_limit = 32 * 1024 * 1024;
inline constexpr std::size_t resource_string_limit = 32768;
inline constexpr std::size_t resource_unit_limit = 524288;
// Empty bundle slots are absent strings. file_offset points to the UTF-16 length word.
ResourceStrings parse_resource_strings(std::span<const std::uint8_t> bytes);
ResourceStrings load_resource_strings(const std::filesystem::path& path);
}
