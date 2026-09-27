#pragma once

#include <filesystem>

namespace saves {
std::filesystem::path prepare_directory(const std::filesystem::path& game);
}
