#pragma once
#include <windows.h>

namespace enhancements::reveal {
void controller(bool held);
bool message(HWND window, UINT message, WPARAM value, LPARAM data);
void update(HWND window, bool enabled);
void suspend();
void release();
}
