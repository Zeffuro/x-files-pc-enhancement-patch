#pragma once

#include <windows.h>
#include <filesystem>

namespace enhancements {
void quick_save(HWND window, bool load);
void update_quick_load();
bool checkpoint_available();
void load_checkpoint(HWND window, const std::filesystem::path& path);
bool export_save_available();
void export_save(const std::filesystem::path& path);
}
