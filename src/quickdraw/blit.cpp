#include "world.h"
#include "regions.h"

#include <stdexcept>

namespace {

using namespace quickdraw;

class Surface {
public:
    Surface(int width, int height) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        dc = CreateCompatibleDC(nullptr);
        bitmap_ = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels),
                                   nullptr, 0);
        if (!dc || !bitmap_) {
            release();
            throw std::runtime_error("CopyBits: cannot allocate blend surface");
        }
        previous_ = SelectObject(dc, bitmap_);
        if (!previous_ || previous_ == HGDI_ERROR) {
            previous_ = nullptr;
            release();
            throw std::runtime_error("CopyBits: cannot select blend surface");
        }
    }

    ~Surface() {
        release();
    }

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    HDC dc = nullptr;
    std::uint8_t* pixels = nullptr;

private:
    void release() {
        if (previous_) {
            SelectObject(dc, previous_);
        }
        if (bitmap_) {
            DeleteObject(bitmap_);
        }
        if (dc) {
            DeleteDC(dc);
        }
    }

    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

BOOL blend(HDC source, HDC destination, const Rect& from, const Rect& to) {
    const int width = to.right - to.left;
    const int height = to.bottom - to.top;
    if (width <= 0 || height <= 0) {
        return TRUE;
    }
    if (width > 8192 || height > 8192) {
        throw std::runtime_error("CopyBits: blend surface exceeds size limit");
    }
    Surface input(width, height);
    Surface output(width, height);
    SetStretchBltMode(input.dc, COLORONCOLOR);
    if (!StretchBlt(input.dc, 0, 0, width, height, source, from.left, from.top,
                    from.right - from.left, from.bottom - from.top, SRCCOPY) ||
        !BitBlt(output.dc, 0, 0, width, height, destination, to.left, to.top, SRCCOPY)) {
        return FALSE;
    }
    GdiFlush();
    const auto color = operation_color();
    const std::uint32_t weights[]{color.blue, color.green, color.red};
    for (int pixel = 0; pixel < width * height; ++pixel) {
        for (int channel = 0; channel < 3; ++channel) {
            const auto index = pixel * 4 + channel;
            const auto weight = weights[channel];
            output.pixels[index] = static_cast<std::uint8_t>(
                (input.pixels[index] * weight + output.pixels[index] * (65535 - weight) + 32767) /
                65535);
        }
    }
    return BitBlt(destination, to.left, to.top, width, height, output.dc, 0, 0, SRCCOPY);
}

void __cdecl copy_bits(const PixMap* source, const PixMap* destination, const Rect* from,
                       const Rect* to, TransferMode mode, RegionHandle mask) {
    const auto source_dc = pixel_dc(source);
    const auto destination_dc = pixel_dc(destination);
    if (!source_dc || !destination_dc || !from || !to ||
        (mode != TransferMode::Copy && mode != TransferMode::DitherCopy &&
         mode != TransferMode::Transparent && mode != TransferMode::Blend)) {
        trace_value("copy_source_known", source_dc != nullptr);
        trace_value("copy_destination_known", destination_dc != nullptr);
        trace_value("copy_mode", static_cast<std::uint16_t>(mode));
        unsupported(Selector::CopyBits, "CopyBits: unsupported bitmap or transfer mode", 0);
    }
    const auto saved = SaveDC(destination_dc);
    if (!saved) {
        unsupported(Selector::CopyBits, "CopyBits: cannot save destination context", 0);
    }
    try {
        if (mask) {
            auto region = native_region(mask);
            POINT origin{};
            GetViewportOrgEx(destination_dc, &origin);
            if (OffsetRgn(region.get(), origin.x, origin.y) == ERROR ||
                ExtSelectClipRgn(destination_dc, region.get(), RGN_AND) == ERROR) {
                throw std::runtime_error("CopyBits: cannot apply clipping mask");
            }
        }
        SetStretchBltMode(destination_dc, COLORONCOLOR);
        BOOL copied;
        if (mode == TransferMode::Blend) {
            copied = blend(source_dc, destination_dc, *from, *to);
        } else if (mode == TransferMode::Transparent) {
            const auto& key = drawing_port().background;
            copied = TransparentBlt(destination_dc, to->left, to->top, to->right - to->left,
                                    to->bottom - to->top, source_dc, from->left, from->top,
                                    from->right - from->left, from->bottom - from->top,
                                    RGB(key.red >> 8, key.green >> 8, key.blue >> 8));
        } else {
            copied = StretchBlt(destination_dc, to->left, to->top, to->right - to->left,
                                to->bottom - to->top, source_dc, from->left, from->top,
                                from->right - from->left, from->bottom - from->top, SRCCOPY);
        }
        GdiFlush();
        if (!copied) {
            unsupported(Selector::CopyBits, "CopyBits: GDI transfer failed", 0);
        }
        present_pixels(destination, *to);
        RestoreDC(destination_dc, saved);
    } catch (const std::exception& error) {
        RestoreDC(destination_dc, saved);
        unsupported(Selector::CopyBits, error.what(), 0);
    }
}

}

namespace quickdraw {

void draw_matte(const PixMap* pixels, const Rect& source, const Rect& destination,
                std::int16_t mode, Region** clip, const PixMap* matte, const Rect* matte_bounds) {
    auto& port = drawing_port();
    if (!port.pixels || !*port.pixels) {
        throw std::runtime_error("StdPix: missing destination pixels");
    }
    if (!matte) {
        copy_bits(pixels, *port.pixels, &source, &destination, static_cast<TransferMode>(mode),
                  clip);
        return;
    }
    const auto input_dc = pixel_dc(pixels);
    const auto output_dc = pixel_dc(*port.pixels);
    const int width = destination.right - destination.left;
    const int height = destination.bottom - destination.top;
    const auto transfer = static_cast<TransferMode>(mode);
    if (!input_dc || !output_dc || !matte_bounds || !matte->base ||
        (matte->pixel_size != 1 && matte->pixel_size != 8) ||
        (transfer != TransferMode::Copy && transfer != TransferMode::DitherCopy) || width <= 0 ||
        height <= 0 || width > 8192 || height > 8192 || matte_bounds->left < matte->bounds.left ||
        matte_bounds->top < matte->bounds.top || matte_bounds->right > matte->bounds.right ||
        matte_bounds->bottom > matte->bounds.bottom || matte_bounds->right <= matte_bounds->left ||
        matte_bounds->bottom <= matte_bounds->top) {
        throw std::runtime_error("StdPix: unsupported matte or transfer mode");
    }
    Surface input(width, height);
    Surface output(width, height);
    SetStretchBltMode(input.dc, COLORONCOLOR);
    if (!StretchBlt(input.dc, 0, 0, width, height, input_dc, source.left, source.top,
                    source.right - source.left, source.bottom - source.top, SRCCOPY) ||
        !BitBlt(output.dc, 0, 0, width, height, output_dc, destination.left, destination.top,
                SRCCOPY)) {
        throw std::runtime_error("StdPix: cannot prepare matte surfaces");
    }
    GdiFlush();
    const int stride = matte->row_bytes & row_bytes_mask;
    const int row_bits = (matte->bounds.right - matte->bounds.left) * matte->pixel_size;
    if (stride < (row_bits + 7) / 8) {
        throw std::runtime_error("StdPix: matte row is shorter than its bounds");
    }
    for (int y = 0; y < height; ++y) {
        const auto my = matte_bounds->top - matte->bounds.top +
                        y * (matte_bounds->bottom - matte_bounds->top) / height;
        for (int x = 0; x < width; ++x) {
            const auto mx = matte_bounds->left - matte->bounds.left +
                            x * (matte_bounds->right - matte_bounds->left) / width;
            const auto row = matte->base + my * stride;
            const unsigned alpha = matte->pixel_size == 8               ? row[mx]
                                   : (row[mx / 8] & (0x80 >> (mx % 8))) ? 255
                                                                        : 0;
            for (int channel = 0; channel < 3; ++channel) {
                const auto index = (y * width + x) * 4 + channel;
                output.pixels[index] = static_cast<std::uint8_t>(
                    (input.pixels[index] * alpha + output.pixels[index] * (255 - alpha) + 127) /
                    255);
            }
        }
    }
    const auto saved = SaveDC(output_dc);
    if (!saved) {
        throw std::runtime_error("StdPix: cannot save drawing context");
    }
    bool copied = false;
    try {
        auto region = native_region(port.clip_region);
        if (OffsetRgn(region.get(), -(*port.pixels)->bounds.left, -(*port.pixels)->bounds.top) ==
                ERROR ||
            ExtSelectClipRgn(output_dc, region.get(), RGN_AND) == ERROR) {
            throw std::runtime_error("StdPix: cannot apply port clipping");
        }
        if (clip) {
            auto mask = native_region(clip);
            if (OffsetRgn(mask.get(), -(*port.pixels)->bounds.left, -(*port.pixels)->bounds.top) ==
                    ERROR ||
                ExtSelectClipRgn(output_dc, mask.get(), RGN_AND) == ERROR) {
                throw std::runtime_error("StdPix: cannot apply matte clipping");
            }
        }
        copied = BitBlt(output_dc, destination.left, destination.top, width, height, output.dc, 0,
                        0, SRCCOPY) != FALSE;
        GdiFlush();
    } catch (...) {
        RestoreDC(output_dc, saved);
        throw;
    }
    RestoreDC(output_dc, saved);
    if (!copied) {
        throw std::runtime_error("StdPix: matte transfer failed");
    }
    present_pixels(*port.pixels, destination);
}

}

Entry blit_entry(Selector selector) {
    static const EntryBinding entries[] = {bind_entry(Selector::CopyBits, copy_bits)};
    return find_entry(selector, entries);
}
