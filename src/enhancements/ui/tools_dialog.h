#pragma once

#include <windows.h>
#include <filesystem>

namespace enhancements {
struct ToolsResult {
    std::filesystem::path checkpoint;
    bool inspect = false;
    bool easter_eggs = false;
};

ToolsResult show_tools_dialog(HWND owner, HMODULE module);
}
