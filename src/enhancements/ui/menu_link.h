#pragma once

#include <windows.h>

namespace enhancements {
inline constexpr RECT settings_link{470, 435, 638, 478};
bool settings_link_visible();
void update_settings_link(HWND owner, bool visible);
void release_settings_link();
}
