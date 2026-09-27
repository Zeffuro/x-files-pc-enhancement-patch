#pragma once

#include <windows.h>

namespace enhancements {

void set_game_string_code_page(UINT code_page) noexcept;
int WINAPI load_game_string(HINSTANCE module, UINT id, LPSTR buffer, int capacity) noexcept;

}
