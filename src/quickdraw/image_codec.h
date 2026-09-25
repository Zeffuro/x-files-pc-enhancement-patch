#pragma once

#include "world.h"
#include <span>

namespace quickdraw {

#pragma pack(push, 2)

struct ImageDescription {
    std::int32_t size;
    std::uint32_t codec;
    std::int32_t reserved;
    std::int16_t reserved2, reference, version, revision;
    std::uint32_t vendor, temporal_quality, spatial_quality;
    std::int16_t width, height;
    std::int32_t horizontal_resolution, vertical_resolution, data_size;
    std::int16_t frames;
    char name[32];
    std::int16_t depth, color_table;
};

#pragma pack(pop)
static_assert(sizeof(ImageDescription) == 86);

bool record_image(const ImageDescription& description, std::span<const std::uint8_t> data,
                  const Rect& source, const Rect& destination, short mode);
void release_recording();

}

Entry image_codec_entry(Selector selector);
Entry recording_entry(Selector selector);
