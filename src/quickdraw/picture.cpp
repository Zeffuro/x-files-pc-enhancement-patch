#include "world.h"
#include "regions.h"
#include "memory.h"
#include "picture/pict.h"
#include "compressed.h"

#include <stdexcept>
#include <intrin.h>

namespace {

using namespace quickdraw;

std::int16_t scale(int value, int source_origin, int source_size, int destination_origin,
                   int destination_size) {
    const auto result =
        destination_origin + std::int64_t(value - source_origin) * destination_size / source_size;
    if (result < -32768 || result > 32767) {
        throw std::runtime_error("DrawPicture: scaled coordinates overflow");
    }
    return static_cast<std::int16_t>(result);
}

Rect transform(const Rect& rectangle, const Rect& source, const Rect& destination) {
    const int width = source.right - source.left;
    const int height = source.bottom - source.top;
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("DrawPicture: invalid picture frame");
    }
    const int output_width = destination.right - destination.left;
    const int output_height = destination.bottom - destination.top;
    return {
        scale(rectangle.top, source.top, height, destination.top, output_height),
        scale(rectangle.left, source.left, width, destination.left, output_width),
        scale(rectangle.bottom, source.top, height, destination.top, output_height),
        scale(rectangle.right, source.left, width, destination.left, output_width),
    };
}

struct SavedDC {
    HDC dc;
    int state;

    explicit SavedDC(HDC context) : dc(context), state(SaveDC(context)) {
        if (!state) {
            throw std::runtime_error("DrawPicture: cannot save the drawing context");
        }
    }

    ~SavedDC() {
        RestoreDC(dc, state);
    }
};

void __cdecl draw_picture(std::uint8_t** handle, const Rect* destination) {
    try {
        if (!destination) {
            throw std::runtime_error("DrawPicture: missing destination rectangle");
        }
        const auto decoded = picture::read(handle_bytes(handle));
        auto& port = drawing_port();
        const auto dc = port_dc(&port);
        if (!dc) {
            throw std::runtime_error("DrawPicture: missing drawing port");
        }
        const auto callback =
            port.procedures && port.procedures->bits ? port.procedures->bits : standard_bits;
        for (const auto& bitmap : decoded.bitmaps) {
            SavedDC saved(dc);
            const auto clip = transform(bitmap.clip, decoded.frame, *destination);
            if (IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom) == ERROR) {
                throw std::runtime_error("DrawPicture: cannot apply the picture clip");
            }
            const auto rectangle = transform(bitmap.destination, decoded.frame, *destination);
            PixMap pixels{};
            pixels.base = const_cast<std::uint8_t*>(bitmap.pixels.data());
            pixels.row_bytes = pixmap_flag | bitmap.stride;
            pixels.bounds = bitmap.bounds;
            pixels.horizontal_resolution = 72 << 16;
            pixels.vertical_resolution = 72 << 16;
            pixels.pixel_type = direct_pixel_type;
            pixels.pixel_size = 32;
            pixels.component_count = static_cast<std::int16_t>(bitmap.components);
            pixels.component_size = 8;
            pixels.pixel_format = bgra_format;
            if (bitmap.compressed.empty()) {
                callback(&pixels, &bitmap.source, &rectangle, bitmap.mode, nullptr);
            } else {
                draw_compressed(bitmap, pixels, rectangle, port.procedures);
            }
        }
    } catch (const std::exception& error) {
        unsupported(Selector::DrawPicture, error.what(), 0);
    }
}

short __cdecl draw_trimmed(std::uint8_t** handle, const Rect* destination, RegionHandle mask, short,
                           void* progress) {
    if (progress) {
        unsupported(Selector::DrawTrimmedPicture,
                    "DrawTrimmedPicture: progress callback unsupported", 0);
    }
    auto& port = drawing_port();
    const auto dc = port_dc(&port);
    if (!dc || !destination) {
        return -50;
    }
    try {
        SavedDC saved(dc);
        if (mask) {
            auto region = native_region(mask);
            if (OffsetRgn(region.get(), -port.bounds.left, -port.bounds.top) == ERROR ||
                ExtSelectClipRgn(dc, region.get(), RGN_AND) == ERROR) {
                return -108;
            }
        }
        draw_picture(handle, destination);
        return 0;
    } catch (const std::bad_alloc&) {
        return -108;
    }
}

}

namespace quickdraw {

void __cdecl standard_bits(const PixMap* pixels, const Rect* source, const Rect* destination,
                           std::int16_t mode, Region** mask) {
    trace_call(static_cast<std::uint32_t>(Selector::SetStdCProcs), "StdBits",
               reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    const auto dc = port_dc(&drawing_port());
    if (!pixels || !pixels->base || !source || !destination || !dc || mask ||
        (mode != static_cast<std::int16_t>(TransferMode::Copy) &&
         mode != static_cast<std::int16_t>(TransferMode::DitherCopy)) ||
        pixels->pixel_size != 32 || pixels->pixel_format != bgra_format) {
        unsupported(Selector::DrawPicture, "StdBits: unsupported bitmap, mode or drawing port", 0);
    }
    const int stride = pixels->row_bytes & row_bytes_mask;
    const int width = pixels->bounds.right - pixels->bounds.left;
    const int height = pixels->bounds.bottom - pixels->bounds.top;
    if (width <= 0 || height <= 0 || stride < width * 4 || stride % 4 ||
        source->left < pixels->bounds.left || source->right > pixels->bounds.right ||
        source->top < pixels->bounds.top || source->bottom > pixels->bounds.bottom) {
        unsupported(Selector::DrawPicture, "StdBits: invalid source dimensions", 0);
    }
    BITMAPINFO description{};
    description.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    description.bmiHeader.biWidth = stride / 4;
    description.bmiHeader.biHeight = -height;
    description.bmiHeader.biPlanes = 1;
    description.bmiHeader.biBitCount = 32;
    description.bmiHeader.biCompression = BI_RGB;
    const auto result = StretchDIBits(
        dc, destination->left, destination->top, destination->right - destination->left,
        destination->bottom - destination->top, source->left - pixels->bounds.left,
        source->top - pixels->bounds.top, source->right - source->left,
        source->bottom - source->top, pixels->base, &description, DIB_RGB_COLORS, SRCCOPY);
    if (result == GDI_ERROR) {
        unsupported(Selector::DrawPicture, "StdBits: drawing failed", 0);
    }
    GdiFlush();
}

}

Entry picture_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::DrawPicture, draw_picture),
        bind_entry(Selector::GetCompressedPixMapInfo, compressed_info),
        bind_entry(Selector::DrawTrimmedPicture, draw_trimmed),
    };
    return find_entry(selector, entries);
}
