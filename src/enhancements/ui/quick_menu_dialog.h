#pragma once

#include <windows.h>

struct Settings;

namespace enhancements {
void show_quick_menu_dialog(HWND owner, HMODULE module, Settings& draft);
}
