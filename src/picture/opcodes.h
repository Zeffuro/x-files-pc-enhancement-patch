#pragma once

#include <cstdint>
#include <cstddef>

namespace picture {

enum class Opcode : std::uint16_t {
    Noop = 0x0000,
    Clip = 0x0001,
    TextFont = 0x0003,
    TextFace = 0x0004,
    PenSize = 0x0007,
    TextSize = 0x000d,
    TextRatio = 0x0010,
    LongText = 0x0028,
    CompressedQuickTime = 0x8200,
    Version = 0x0011,
    DefaultHighlight = 0x001e,
    PackBitsRect = 0x0098,
    DirectBitsRect = 0x009a,
    ShortComment = 0x00a0,
    LongComment = 0x00a1,
    End = 0x00ff,
    Header = 0x0c00,
};

enum class Packing : std::uint16_t {
    Unpacked = 1,
    DropPadding = 2,
    PixelPackBits = 3,
    ComponentPackBits = 4,
};

inline constexpr std::uint16_t version_two = 0x02ff;
inline constexpr std::size_t extended_header_size = 24;

}
