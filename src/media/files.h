#pragma once

#include <filesystem>
#include <windows.h>

namespace media {

std::filesystem::path locate_file(const std::filesystem::path& requested);
bool install_file_hooks(HMODULE executable);
void remove_file_hooks();

}
