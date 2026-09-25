#pragma once

#include <cstddef>
#include <cstdint>

namespace quickdraw {

inline constexpr std::uint16_t pixmap_flag = 0x8000;
inline constexpr std::uint16_t row_bytes_mask = 0x3fff;
inline constexpr std::uint32_t bgra_format = 0x42475241;
inline constexpr std::uint32_t bgr_format = 0x32344247;
inline constexpr std::uint32_t monochrome_format = 1;
inline constexpr std::uint32_t indexed_format = 8;
inline constexpr std::int16_t direct_pixel_type = 16;

enum class TransferMode : std::int16_t { Copy = 0, Blend = 32, Transparent = 36, DitherCopy = 64 };

#pragma pack(push, 2)

struct Point {
    std::int16_t y;
    std::int16_t x;
};

struct Rect {
    std::int16_t top;
    std::int16_t left;
    std::int16_t bottom;
    std::int16_t right;
};

struct Color {
    std::uint16_t red;
    std::uint16_t green;
    std::uint16_t blue;
};

struct ColorSpec {
    std::int16_t value;
    Color color;
};

struct ColorTable {
    std::int32_t seed;
    std::uint16_t flags;
    std::int16_t last_entry;
    ColorSpec colors[256];
};

using ColorTableHandle = ColorTable**;

struct Region {
    std::int16_t size;
    Rect bounds;
};

struct PixMap {
    std::uint8_t* base;
    std::uint16_t row_bytes;
    Rect bounds;
    std::int16_t version;
    std::int16_t pack_type;
    std::int32_t pack_size;
    std::int32_t horizontal_resolution;
    std::int32_t vertical_resolution;
    std::int16_t pixel_type;
    std::int16_t pixel_size;
    std::int16_t component_count;
    std::int16_t component_size;
    std::uint32_t pixel_format;
    void* color_table;
    void* extension;
};

using PixMapHandle = PixMap**;

enum class DeviceType : std::int16_t { Fixed = 0, Indexed = 1, Direct = 2 };

struct Device {
    std::int16_t reference;
    std::int16_t id;
    DeviceType type;
    void* inverse_table;
    std::int16_t resolution;
    void* search;
    void* complement;
    std::int16_t flags;
    PixMapHandle pixels;
    std::int32_t context;
    Device** next;
    Rect bounds;
    std::int32_t mode;
    std::int16_t cursor_bytes;
    std::int16_t cursor_depth;
    void* cursor_data;
    void* cursor_mask;
    void* extension;
};

using DeviceHandle = Device**;

enum class Verb : std::int8_t { Frame, Paint, Erase, Invert, Fill };

using Procedure = void(__cdecl*)();
using RectangleProcedure = void(__cdecl*)(Verb, const Rect*);
using BitsProcedure = void(__cdecl*)(const PixMap*, const Rect*, const Rect*, std::int16_t,
                                     Region**);

struct Matrix {
    std::int32_t values[9];
};

using PixelsProcedure = void(__cdecl*)(const PixMap*, const Rect*, const Matrix*, std::int16_t,
                                       Region**, const PixMap*, const Rect*, std::int16_t);

struct Procedures {
    Procedure text;
    Procedure line;
    RectangleProcedure rectangle;
    Procedure rounded_rectangle;
    Procedure oval;
    Procedure arc;
    Procedure polygon;
    Procedure region;
    BitsProcedure bits;
    Procedure comment;
    Procedure text_measurement;
    Procedure get_picture;
    Procedure put_picture;
    Procedure opcode;
    PixelsProcedure pixels;
    Procedure glyphs;
    Procedure printer_status;
    Procedure reserved[3];
};

struct Port {
    std::int16_t device;
    PixMapHandle pixels;
    std::uint16_t version;
    void* variables;
    std::int16_t character_extra;
    std::int16_t pen_fraction;
    Rect bounds;
    Region** visible_region;
    Region** clip_region;
    void* background_pattern;
    Color foreground;
    Color background;
    Point pen_position;
    Point pen_size;
    std::int16_t pen_mode;
    void* pen_pattern;
    void* fill_pattern;
    std::int16_t pen_visibility;
    std::int16_t text_font;
    std::int16_t text_face;
    std::int16_t text_mode;
    std::int16_t text_size;
    std::int32_t space_extra;
    std::int32_t foreground_color;
    std::int32_t background_color;
    std::int16_t color_bit;
    std::int16_t pattern_stretch;
    void* picture_save;
    void* region_save;
    void* polygon_save;
    Procedures* procedures;
};

#pragma pack(pop)

static_assert(sizeof(PixMap) == 50);
static_assert(sizeof(Device) == 62);
static_assert(offsetof(Device, type) == 4);
static_assert(offsetof(Device, pixels) == 22);
static_assert(sizeof(Port) == 108);
static_assert(sizeof(Procedures) == 80);
static_assert(offsetof(Procedures, bits) == 0x20);
static_assert(offsetof(Procedures, pixels) == 0x38);
static_assert(offsetof(Port, procedures) == 0x68);
static_assert(offsetof(Port, pixels) == 2);
static_assert(offsetof(Port, bounds) == 16);
static_assert(offsetof(PixMap, pixel_format) == 38);

}
