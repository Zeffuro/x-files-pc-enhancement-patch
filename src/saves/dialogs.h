#pragma once

#include <windows.h>

namespace saves {
bool install_dialog_hooks(HMODULE executable);
void remove_dialog_hooks();
}
