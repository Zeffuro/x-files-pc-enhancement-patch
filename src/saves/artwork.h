#pragma once
#include <windows.h>
#include <filesystem>
#include <string_view>

namespace saves {
bool draw_artwork(HDC dc, const std::filesystem::path& game, std::string_view filename,
                  const RECT& destination);
}
