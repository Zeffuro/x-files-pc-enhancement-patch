#include "game/render/caption_surface.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>

namespace {
namespace captions = native_game::caption_surface;

struct Surface {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;

    Surface() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 640;
        info.bmiHeader.biHeight = -480;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!dc || !bitmap) {
            throw std::runtime_error("Cannot create caption composition surface");
        }
        previous = SelectObject(dc, bitmap);
    }

    ~Surface() {
        captions::forget(bitmap);
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
};

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

constexpr RECT full{0, 0, 640, 480};
constexpr RECT caption{100, 180, 300, 200};
constexpr RECT credit{150, 150, 250, 190};

void run() {
    Surface movie;
    Surface cached;
    Surface canvas;
    captions::paint(movie.dc, full, caption);
    require(captions::credit_offset(movie.dc, credit) == -14,
            "Credit did not move just above the caption with a four-pixel gap");
    require(captions::credit_offset(movie.dc, {10, 150, 90, 190}) == 0,
            "Non-overlapping credit moved");

    captions::copy(cached.dc, full, movie.dc, full, SRCCOPY);
    captions::forget(movie.bitmap);
    captions::copy(canvas.dc, full, cached.dc, full, SRCCOPY);
    require(captions::credit_offset(canvas.dc, credit) == -14,
            "Cached background lost its caption after movie disposal");
    captions::paint(cached.dc, full);
    captions::copy(canvas.dc, full, cached.dc, full, SRCCOPY);
    require(captions::credit_offset(canvas.dc, credit) == 0,
            "Caption-free background retained a stale collision");

    captions::paint(movie.dc, full, caption);
    captions::copy(canvas.dc, {0, 0, 320, 240}, movie.dc, full, SRCCOPY);
    require(captions::credit_offset(canvas.dc, {75, 75, 125, 95}) == -9,
            "Scaled movie did not transform caption bounds");
    captions::paint(canvas.dc, full);
    SetViewportOrgEx(movie.dc, -10, -20, nullptr);
    captions::paint(movie.dc, {10, 20, 650, 500}, RECT{110, 200, 310, 220});
    captions::copy(canvas.dc, full, movie.dc, {10, 20, 650, 500}, SRCCOPY);
    require(captions::credit_offset(canvas.dc, credit) == -14,
            "Port origin changed final caption coordinates");
    SetViewportOrgEx(movie.dc, 0, 0, nullptr);

    captions::paint(canvas.dc, full);
    auto clip = CreateRectRgn(100, 180, 200, 200);
    auto other = CreateRectRgn(250, 180, 300, 200);
    CombineRgn(clip, clip, other, RGN_OR);
    SelectClipRgn(canvas.dc, clip);
    captions::copy(canvas.dc, full, movie.dc, full, SRCCOPY);
    SelectClipRgn(canvas.dc, nullptr);
    DeleteObject(other);
    DeleteObject(clip);
    require(captions::credit_offset(canvas.dc, {210, 150, 240, 190}) == 0,
            "Disjoint dirty clip invented subtitle pixels in its gap");
    require(captions::credit_offset(canvas.dc, {150, 150, 190, 190}) == -14,
            "Clipped copy lost actual caption pixels");
    captions::paint(canvas.dc, {0, 0, 220, 480});
    require(captions::credit_offset(canvas.dc, {150, 150, 190, 190}) == 0,
            "Partial background paint retained erased caption pixels");
    require(captions::credit_offset(canvas.dc, {260, 150, 290, 190}) == -14,
            "Partial repaint erased untouched caption provenance");
    captions::copy(canvas.dc, full, cached.dc, full, SRCCOPY);
    require(captions::credit_offset(canvas.dc, credit) == 0,
            "A hidden or replaced caption kept moving credits");

    captions::paint(canvas.dc, full, caption);
    captions::copy(canvas.dc, {100, 100, 400, 300}, canvas.dc, {0, 0, 300, 200}, SRCCOPY);
    require(captions::credit_offset(canvas.dc, {250, 250, 350, 290}) == -14,
            "Same-bitmap cache copy lost source provenance before reading it");
    captions::clear();
    require(!captions::any(), "Caption surface reset retained state");
}
}

int main() {
    try {
        run();
        std::cout << "Native caption composition passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
