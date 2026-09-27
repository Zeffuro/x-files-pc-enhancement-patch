#include "inspector_internal.h"
#include "playback/caption_paint.h"
#include <algorithm>

namespace devtools::inspector {
void paint_frame(HDC destination, RECT bounds, const media::Frame* frame,
                 const std::wstring& caption) {
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    const auto dc = CreateCompatibleDC(destination);
    const auto bitmap = CreateCompatibleBitmap(destination, width, height);
    if (!dc || !bitmap) {
        if (dc) {
            DeleteDC(dc);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        return;
    }
    const auto previous = SelectObject(dc, bitmap);
    RECT canvas{0, 0, width, height};
    RECT picture = canvas;
    FillRect(dc, &canvas, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    if (frame && frame->width && frame->height && !frame->pixels.empty()) {
        const auto scale = std::min(static_cast<double>(width) / frame->width,
                                    static_cast<double>(height) / frame->height);
        const int image_width = static_cast<int>(frame->width * scale),
                  image_height = static_cast<int>(frame->height * scale);
        picture = {(width - image_width) / 2, (height - image_height) / 2,
                   (width + image_width) / 2, (height + image_height) / 2};
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = frame->width;
        info.bmiHeader.biHeight = -static_cast<LONG>(frame->height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        SetStretchBltMode(dc, HALFTONE);
        StretchDIBits(dc, (width - image_width) / 2, (height - image_height) / 2, image_width,
                      image_height, 0, 0, frame->width, frame->height, frame->pixels.data(), &info,
                      DIB_RGB_COLORS, SRCCOPY);
    }
    const auto* live = selected_movie();
    if ((!frame || frame->pixels.empty()) &&
        (state.player ? state.player->has_audio() : live && live->audio)) {
        const auto brush = CreateSolidBrush(RGB(69, 199, 187));
        for (int i = -4; i <= 4; ++i) {
            const int bar_height = 14 + (4 - std::abs(i)) * 10;
            RECT bar{width / 2 + i * 14 - 4, height / 2 - bar_height / 2, width / 2 + i * 14 + 4,
                     height / 2 + bar_height / 2};
            FillRect(dc, &bar, brush);
        }
        DeleteObject(brush);
    }
    if (!caption.empty()) {
        RECT text{picture.left, picture.top, picture.right, picture.bottom};
        playback::paint_caption(dc, text, caption, settings().caption_style,
                                picture.right - picture.left);
    }
    BitBlt(destination, bounds.left, bounds.top, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
}
}
