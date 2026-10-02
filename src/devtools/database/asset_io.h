#pragma once
#include "media/video.h"
#include <filesystem>
#include <span>

namespace devtools {
std::vector<std::uint8_t> read_asset_bytes(const std::filesystem::path& path,
                                           std::size_t limit = 64 * 1024 * 1024);
void write_new_asset(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
media::Frame read_asset_png(const std::filesystem::path& path);
std::vector<std::uint8_t> asset_png_bytes(const media::Frame& frame);
}
