#pragma once
#include <windows.h>

namespace enhancements {
void update_autosave(HWND window, bool blocked);
void autosave_loaded();
void release_autosave();
}
