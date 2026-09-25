#pragma once

#include <windows.h>
#include <array>
#include <string>

namespace enhancements {

inline constexpr char keyboard_letters[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM,()";
inline constexpr unsigned keyboard_letter_count = sizeof(keyboard_letters) - 1;
inline constexpr unsigned keyboard_key_count = keyboard_letter_count + 4;
inline constexpr RECT keyboard_panel{90, 354, 550, 477};

std::array<RECT, keyboard_key_count> keyboard_bounds();
void show_keyboard(HWND owner, bool save_menu, unsigned selected);
void hide_keyboard();
void release_keyboard();

}
