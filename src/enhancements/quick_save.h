#pragma once

#include <windows.h>
#include <filesystem>

namespace enhancements {
std::filesystem::path save_game_root();
bool safe_save_available();
void export_safe_save(const std::filesystem::path& path);
void quick_save(HWND window, bool load);
void update_quick_load();
bool checkpoint_available();
bool checkpoint_load_pending();
void load_checkpoint(HWND window, const std::filesystem::path& path);
bool export_save_available();
void export_save(const std::filesystem::path& path);
}
