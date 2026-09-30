#pragma once

#include <windows.h>

namespace enhancements {

bool equip_controller_gun();
bool controller_gun_busy();
void refresh_controller_gun_cursor(POINT position);
bool refresh_native_cursor(POINT position);

}
