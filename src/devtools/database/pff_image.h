#pragma once
#include "media/video.h"
#include <span>

namespace devtools {
// PFF entries contain raw PICT data, without a standalone PICT file header.
media::Frame decode_pff_image(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_pff_image(const media::Frame& frame);
}
