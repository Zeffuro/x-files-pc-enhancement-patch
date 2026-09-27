#pragma once
#include <filesystem>
#include <array>

bool show_welcome(const std::filesystem::path& directory);
bool save_welcome_choices(const std::filesystem::path& settings_file,
                          const std::array<bool, 5>& enabled);
