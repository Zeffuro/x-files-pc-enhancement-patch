#pragma once

#include <filesystem>
#include <string_view>

namespace diagnostics {
inline constexpr std::uintmax_t log_limit = 2 * 1024 * 1024;
std::filesystem::path log_directory(const std::filesystem::path& game_directory);
std::filesystem::path prepare_log_directory(const std::filesystem::path& game_directory);
bool append_log(const std::filesystem::path& path, std::string_view line) noexcept;
void preserve_crash_logs(const std::filesystem::path& directory) noexcept;
bool copy_log_tail(const std::filesystem::path& source,
                   const std::filesystem::path& target) noexcept;
}
