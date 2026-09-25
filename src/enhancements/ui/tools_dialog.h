#pragma once

#include <windows.h>
#include <filesystem>

namespace enhancements {
std::filesystem::path show_tools_dialog(HWND owner, HMODULE module);
}
