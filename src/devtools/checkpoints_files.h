#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace devtools {
struct CheckpointFiles {
    std::filesystem::path executable, baseline;
    std::string build;
};

std::filesystem::path checkpoint_module_file(HMODULE module);
std::filesystem::path checkpoint_recipe_folder(HMODULE module);
CheckpointFiles find_checkpoint_files(HMODULE module, const std::filesystem::path& root);
}
