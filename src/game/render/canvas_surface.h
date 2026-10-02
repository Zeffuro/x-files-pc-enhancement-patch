#pragma once
#include <windows.h>

namespace native_game {
class CanvasSurface {
public:
    CanvasSurface() {
        dc_ = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 640;
        info.bmiHeader.biHeight = -480;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (dc_ && bitmap_) {
            previous_ = SelectObject(dc_, bitmap_);
        }
    }

    ~CanvasSurface() {
        if (previous_ && previous_ != HGDI_ERROR) {
            SelectObject(dc_, previous_);
        }
        if (bitmap_) {
            DeleteObject(bitmap_);
        }
        if (dc_) {
            DeleteDC(dc_);
        }
    }

    CanvasSurface(const CanvasSurface&) = delete;
    CanvasSurface& operator=(const CanvasSurface&) = delete;

    HDC copy(HDC source) const {
        return dc_ && bitmap_ && previous_ && previous_ != HGDI_ERROR && source &&
                       BitBlt(dc_, 0, 0, 640, 480, source, 0, 0, SRCCOPY)
                   ? dc_
                   : nullptr;
    }

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};
}
