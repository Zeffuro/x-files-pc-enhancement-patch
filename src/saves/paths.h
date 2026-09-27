#pragma once

#include <filesystem>

namespace saves {
std::filesystem::path redirected_path(const std::filesystem::path& directory,
                                      const std::filesystem::path& requested);
std::filesystem::path redirected_path(const char* requested) noexcept;
}
