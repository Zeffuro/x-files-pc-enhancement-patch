#pragma once
#include <windows.h>

namespace native_game {
using CanvasSource = HDC (*)(HDC native_canvas);
void attach_native_render();
void detach_native_render();
bool native_render_available();
void set_canvas_source(CanvasSource callback);
HDC canvas_dc();
void invalidate_canvas();
}
