#pragma once

#include "quickdraw/types.h"

#include <span>
#include <vector>

namespace picture {

struct Bitmap {
    quickdraw::Rect bounds;
    quickdraw::Rect source;
    quickdraw::Rect destination;
    quickdraw::Rect clip;
    std::int16_t mode;
    std::uint16_t stride;
    std::uint16_t components;
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> description;
    std::vector<std::uint8_t> compressed;
};

struct Picture {
    quickdraw::Rect frame;
    std::vector<Bitmap> bitmaps;
};

Picture read(std::span<const std::uint8_t> bytes);

}
