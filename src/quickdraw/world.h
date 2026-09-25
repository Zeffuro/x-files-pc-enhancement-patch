#pragma once

#include "runtime.h"
#include "types.h"

#include <windows.h>

namespace quickdraw {

Port& drawing_port();
Color& operation_color();
void draw_matte(const PixMap* pixels, const Rect& source, const Rect& destination,
                std::int16_t mode, Region** clip, const PixMap* matte, const Rect* matte_bounds);
HDC port_dc(Port* port);
HDC pixel_dc(const PixMap* pixels);
void present_port(Port* port, const Rect& bounds);
void present_pixels(const PixMap* pixels, const Rect& bounds);
void __cdecl standard_rectangle(Verb verb, const Rect* rectangle);
void __cdecl standard_pixels(const PixMap*, const Rect*, const Matrix*, std::int16_t, Region**,
                             const PixMap*, const Rect*, std::int16_t);
void __cdecl standard_bits(const PixMap* pixels, const Rect* source, const Rect* destination,
                           std::int16_t mode, Region** mask);

}

Entry world_entry(Selector selector);
Entry drawing_entry(Selector selector);
Entry procedures_entry(Selector selector);
Entry picture_entry(Selector selector);
Entry blit_entry(Selector selector);
void release_worlds();
