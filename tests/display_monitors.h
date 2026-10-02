#pragma once

#include <vector>
#include <windows.h>

std::vector<MONITORINFO> synthetic_display_monitors();
void install_display_monitors(HMODULE library);
void remove_display_monitors();
void test_monitor_restore_moves(HWND window, decltype(&SetWindowPos) set_window_pos,
                                void (*pump)());
