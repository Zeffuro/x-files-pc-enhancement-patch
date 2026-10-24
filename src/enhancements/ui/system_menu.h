#pragma once
#include "localization/ui.h"

namespace enhancements::system_menu {
inline constexpr UINT settings_command = 0x1ff0;
inline constexpr UINT inspector_command = 0x1fe0;

bool refresh(HWND window, ui::Language language);
}
