#pragma once

#include <windows.h>
#include <filesystem>
#include <string_view>

namespace diagnostics {
std::filesystem::path choose_save_file(HWND owner, bool writing,
                                       const std::filesystem::path& directory);
void save_report_dialog(HWND owner, const std::filesystem::path& directory,
                        std::string_view details, bool include_save);
void show_crash_report(const std::filesystem::path& directory, unsigned exit_code);
}
