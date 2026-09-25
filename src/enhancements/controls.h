#pragma once

#include <windows.h>

namespace enhancements {

void attach_controls(HWND window);
void detach_controls();
void request_settings(HWND window);
bool game_is_foreground(HWND window);
void poll_controller(HWND window);
void move_analog_cursor(HWND window, SHORT horizontal, SHORT vertical, float elapsed);
void suspend_analog_cursor();

}
