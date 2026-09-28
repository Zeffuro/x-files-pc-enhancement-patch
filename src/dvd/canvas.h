#pragma once

#include <stdexcept>
#include <windows.h>

namespace dvd {

class Canvas {
public:
    Canvas(int width, int height) : size{width, height} {
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        bitmap_ = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (dc && bitmap_) {
            previous_ = SelectObject(dc, bitmap_);
        }
        if (!dc || !bitmap_ || !previous_ || previous_ == HGDI_ERROR) {
            release();
            throw std::runtime_error("Could not create DVD presentation buffer");
        }
    }

    ~Canvas() {
        release();
    }

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    HDC dc = nullptr;
    SIZE size{};

private:
    void release() noexcept {
        if (dc && previous_ && previous_ != HGDI_ERROR) {
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

}
