#pragma once

#include <windows.h>

struct Settings;

namespace enhancements {
void show_controller_dialog(HWND owner, HMODULE module, Settings& draft);
}
