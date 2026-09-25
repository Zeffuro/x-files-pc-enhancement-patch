#include "compressed.h"
#include "matrix.h"
#include "world.h"
#include "media/video.h"

#include <stdexcept>
#include <intrin.h>

namespace quickdraw {
namespace {

struct Context {
    const PixMap* pixels;
    const picture::Bitmap* bitmap;
    std::uint8_t* description;
    Context* previous;
};

thread_local Context* current = nullptr;

const Context* context(const PixMap* pixels) {
    for (auto* item = current; item; item = item->previous) {
        if (item->pixels == pixels) {
            return item;
        }
    }
    return nullptr;
}

}

short __cdecl compressed_info(const PixMap* pixels, std::uint8_t*** description,
                              std::uint8_t** data, std::int32_t* size, void* data_proc,
                              void* progress_proc) {
    const auto* item = context(pixels);
    if (!item || data_proc || progress_proc) {
        return -50;
    }
    if (description) {
        *description = const_cast<std::uint8_t**>(&item->description);
    }
    if (data) {
        *data = const_cast<std::uint8_t*>(item->bitmap->compressed.data());
    }
    if (size) {
        *size = static_cast<std::int32_t>(item->bitmap->compressed.size());
    }
    return 0;
}

void draw_compressed(const picture::Bitmap& bitmap, PixMap& pixels, const Rect& destination,
                     const Procedures* procedures) {
    Context item{&pixels, &bitmap, const_cast<std::uint8_t*>(bitmap.description.data()), current};

    struct Restore {
        Context* previous;

        ~Restore() {
            current = previous;
        }
    } restore{current};

    current = &item;
    const auto& source = bitmap.source;
    const auto matrix = rectangle_matrix(source, destination);
    const auto callback = procedures && procedures->pixels ? procedures->pixels : standard_pixels;
    callback(&pixels, &source, &matrix, bitmap.mode, nullptr, nullptr, nullptr, 0);
}

void __cdecl standard_pixels(const PixMap* pixels, const Rect* source, const Matrix* matrix,
                             std::int16_t mode, Region** mask, const PixMap* matte,
                             const Rect* matte_bounds, std::int16_t flags) {
    trace_call(static_cast<std::uint32_t>(Selector::SetStdCProcs), "StdPix",
               reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    const auto* item = context(pixels);
    if (!pixels || !matrix || (flags & ~3)) {
        unsupported(Selector::DrawPicture, "StdPix: invalid pixels, matrix or flags", 0);
    }
    if (!source) {
        source = &pixels->bounds;
    }
    if (!item) {
        try {
            const auto destination = transform_rectangle(*source, *matrix);
            draw_matte(pixels, *source, destination, mode, mask, matte,
                       matte_bounds ? matte_bounds
                       : matte      ? &matte->bounds
                                    : nullptr);
        } catch (const std::exception& error) {
            unsupported(Selector::DrawPicture, error.what(), 0);
        }
        return;
    }
    if (mask || matte || flags) {
        unsupported(Selector::DrawPicture, "StdPix: unsupported compressed drawing request", 0);
    }
    const auto& bitmap = *item->bitmap;
    media::Description format;
    for (int byte = 7; byte >= 4; --byte) {
        format.codec += static_cast<char>(bitmap.description[byte]);
    }
    format.width = static_cast<std::uint16_t>(bitmap.bounds.right);
    format.height = static_cast<std::uint16_t>(bitmap.bounds.bottom);
    format.depth = 24;
    media::Video decoder;
    const auto& frame = decoder.image(format, bitmap.compressed);
    PixMap decoded = *pixels;
    decoded.base = const_cast<std::uint8_t*>(frame.pixels.data());
    const auto destination = transform_rectangle(*source, *matrix);
    standard_bits(&decoded, source, &destination, mode, nullptr);
}

}
