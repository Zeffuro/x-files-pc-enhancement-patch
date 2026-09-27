#pragma once
#include <windows.h>

namespace saves {
void update_browser(HWND game);
bool browser_message(HWND game, UINT message, WPARAM value, LPARAM data);
bool browser_active();
HDC browser_canvas(HDC native);
void release_browser();
}
