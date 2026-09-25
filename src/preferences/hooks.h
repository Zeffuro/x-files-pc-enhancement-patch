#pragma once

#include <windows.h>

bool install_preferences_hooks(HMODULE executable, const wchar_t* path);
void remove_preferences_hooks();

namespace preferences {

extern wchar_t settings_path[32768];
FARPROC registry_hook(const char* name);

}
