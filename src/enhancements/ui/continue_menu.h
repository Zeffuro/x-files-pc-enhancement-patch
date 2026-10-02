#pragma once
#include <windows.h>

namespace enhancements {
bool continue_message(HWND window, UINT message, WPARAM value, LPARAM data);
void release_continue_menu();
}
