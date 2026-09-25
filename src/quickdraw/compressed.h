#pragma once

#include "types.h"
#include "picture/pict.h"

namespace quickdraw {

void draw_compressed(const picture::Bitmap& bitmap, PixMap& pixels, const Rect& destination,
                     const Procedures* procedures);
short __cdecl compressed_info(const PixMap* pixels, std::uint8_t*** description,
                              std::uint8_t** data, std::int32_t* size, void* data_proc,
                              void* progress_proc);

}
