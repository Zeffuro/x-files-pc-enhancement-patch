#pragma once

#include <filesystem>

namespace dvd {
std::filesystem::path movie_path(const std::filesystem::path& requested);
void acquire_file_hook();
void release_file_hook();
}
