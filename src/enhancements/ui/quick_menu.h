#pragma once
#include <windows.h>
#include <vector>

namespace enhancements::quick_menu {
void update(bool focused = true);
void release();
void dismiss();
bool expanded();
bool message(HWND window, UINT message, WPARAM value, LPARAM data);
HDC canvas(HDC background);
std::vector<RECT> targets();
}
