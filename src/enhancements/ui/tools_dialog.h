#pragma once

#include <windows.h>
#include <filesystem>

namespace enhancements {
struct ToolsResult {
    std::filesystem::path checkpoint;
    bool inspect = false;
};

ToolsResult show_tools_dialog(HWND owner, HMODULE module);
}
