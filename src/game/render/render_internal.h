#pragma once
#include <windows.h>

namespace native_game {
bool attach_render_imports();
void detach_render_imports();
HDC presentation_source(HDC source);
}
