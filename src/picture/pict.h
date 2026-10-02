#pragma once

#include "quickdraw/types.h"

#include <span>
#include <vector>
#include <limits>

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

struct ReadLimits {
    std::size_t bitmap_count = std::numeric_limits<std::size_t>::max();
    std::size_t bitmap_bytes = std::numeric_limits<std::size_t>::max();
    bool allow_solid_rectangles = false;
};

Picture read(std::span<const std::uint8_t> bytes, ReadLimits limits = {});

}
