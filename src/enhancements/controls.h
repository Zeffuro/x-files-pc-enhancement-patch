#pragma once

#include <windows.h>

namespace enhancements {

void attach_controls(HWND window);
void detach_controls();
void request_settings(HWND window);
bool game_is_foreground(HWND window);
HWND playback_input_window();
void poll_controller(HWND window);
void suspend_controller();
void move_analog_cursor(HWND window, SHORT horizontal, SHORT vertical, float elapsed);
void suspend_analog_cursor();
void begin_controller_inventory_click(HWND window, POINT scene_cursor, POINT item_cursor);
void begin_controller_inventory_click(HWND window, POINT item_cursor);
void cancel_controller_inventory_click();
bool controller_inventory_click_pending();

}
