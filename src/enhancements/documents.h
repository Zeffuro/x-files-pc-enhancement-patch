#pragma once
#include <windows.h>

namespace enhancements::documents {
bool available();
bool show();
bool active();
bool message(HWND window, UINT message, WPARAM value, LPARAM data);
void update(HWND window, bool enabled = true);
void release(bool abandon = false);
}
