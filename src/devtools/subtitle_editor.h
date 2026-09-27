#pragma once
#include "preview.h"
#include <windows.h>

namespace devtools {
void edit_subtitles(HWND owner, HWND game, HMODULE module, const std::filesystem::path& root,
                    std::filesystem::path& relative, std::unique_ptr<Preview>& preview,
                    const std::vector<std::filesystem::path>& movies);
}
