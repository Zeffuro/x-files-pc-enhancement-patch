#pragma once

#include <windows.h>
#include <filesystem>

namespace enhancements {
void subtitle_dialog(HWND owner, HMODULE module, const std::filesystem::path& directory,
                     bool install);
}
