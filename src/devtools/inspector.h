#pragma once

#include <windows.h>

namespace devtools {
void request_inspector();
void pump_inspector_messages();
void update_inspector(HWND game, bool available);
void release_inspector();
}
