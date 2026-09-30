#pragma once
#include <windows.h>

namespace transcript {
bool available();
bool show();
bool active();
bool message(HWND window, UINT message, WPARAM value, LPARAM data);
void update();
void release();
}
