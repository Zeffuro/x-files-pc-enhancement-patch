#pragma once

#include "runtime.h"
#include "quickdraw/types.h"
#include <windows.h>

Entry graphics_entry(Selector selector);
void release_graphics();
void present_graphics(HWND window, HDC source, const quickdraw::Rect& bounds);
