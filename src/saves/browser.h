#pragma once
#include <windows.h>
#include "slots.h"
#include "preview.h"

namespace saves {
enum class BrowserInput { keyboard, controller };
void update_browser(HWND game);
bool browser_message(HWND game, UINT message, WPARAM value, LPARAM data,
                     BrowserInput input = BrowserInput::keyboard);
bool browser_active();
bool show_browser(bool saving);
HDC browser_canvas(HDC native);
void release_browser();
Thumbnail browser_scene_thumbnail();
SceneReference browser_scene_reference();
}
