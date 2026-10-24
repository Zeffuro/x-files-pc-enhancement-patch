#pragma once

#include <windows.h>
#include <string_view>

namespace enhancements {

void set_game_string_code_page(UINT code_page) noexcept;
UINT game_string_code_page() noexcept;
UINT game_resource_code_page(std::string_view text_hash, bool japanese) noexcept;
void set_game_string_dvd_fallback(bool enabled) noexcept;
int WINAPI load_game_string(HINSTANCE module, UINT id, LPSTR buffer, int capacity) noexcept;
int WINAPI draw_game_text(HDC dc, LPCSTR text, int length, LPRECT bounds, UINT format) noexcept;
BOOL WINAPI output_game_text(HDC dc, int x, int y, LPCSTR text, int length) noexcept;
BOOL WINAPI measure_game_text(HDC dc, LPCSTR text, int length, LPSIZE size) noexcept;

}
