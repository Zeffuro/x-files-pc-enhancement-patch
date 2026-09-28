#pragma once
#include <windows.h>

namespace native_game {
using CanvasSource = HDC (*)(HDC native_canvas);

class CanvasPresentation {
public:
    explicit CanvasPresentation(HDC source);
    ~CanvasPresentation();
    CanvasPresentation(const CanvasPresentation&) = delete;
    CanvasPresentation& operator=(const CanvasPresentation&) = delete;

private:
    HDC previous_;
};

void attach_native_render();
void detach_native_render();
bool native_render_available();
void set_canvas_source(CanvasSource callback);
HDC canvas_dc();
void invalidate_canvas();
}
