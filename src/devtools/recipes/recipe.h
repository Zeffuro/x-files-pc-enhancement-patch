#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devtools::recipes {
using Bytes = std::vector<std::uint8_t>;

std::string identify_executable(const std::filesystem::path& path);
std::string extract(const std::filesystem::path& source, const std::filesystem::path& executable,
                    const std::filesystem::path& baseline);
Bytes reconstruct(const std::filesystem::path& source, const std::filesystem::path& executable,
                  const std::filesystem::path& baseline);
void write_new(const std::filesystem::path& path, std::span<const std::uint8_t> data,
               const std::vector<std::filesystem::path>& inputs = {});

std::string extract_data(const Bytes& baseline, const Bytes& saved, std::string_view build);
Bytes reconstruct_data(const Bytes& baseline, std::string_view recipe, std::string_view build);
}
