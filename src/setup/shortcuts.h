#pragma once

#include <filesystem>

void write_shortcut(const std::filesystem::path& link, const std::filesystem::path& game);
void create_shortcuts(const std::filesystem::path& game, bool desktop, bool start_menu);
