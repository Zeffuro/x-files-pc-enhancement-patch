#include "game/render/native_render.h"
#include "game/render/render_internal.h"
#include "enhancements/game_ui.h"
#include "quickdraw/world.h"
#include <iostream>
#include <stdexcept>

namespace enhancements::game {
MainView* current_view() {
    return nullptr;
}

std::byte* executable_image() {
    return nullptr;
}

const Edition& edition() {
    return dvd;
}
}

namespace quickdraw {
HDC port_dc(Port*) {
    return nullptr;
}
}

void trace_value(const char*, std::uint32_t) {}

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Surface {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;

    Surface(COLORREF color) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 64;
        info.bmiHeader.biHeight = -48;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        require(dc && bitmap, "Cannot create presentation fixture");
        previous = SelectObject(dc, bitmap);
        const RECT bounds{0, 0, 64, 48};
        const auto brush = CreateSolidBrush(color);
        FillRect(dc, &bounds, brush);
        DeleteObject(brush);
    }

    ~Surface() {
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
};

HDC replacement = nullptr;
bool fail = false;
unsigned calls = 0;

HDC browser(HDC native) {
    ++calls;
    require(native_game::presentation_source(native) == native,
            "Browser callback recursively substituted itself");
    if (fail) {
        throw std::runtime_error("Unavailable browser surface");
    }
    return replacement;
}

void run() {
    constexpr auto menu = RGB(180, 20, 10), overlay = RGB(10, 40, 200);
    Surface canvas(menu), output(0), browser_surface(overlay), unrelated(0);
    replacement = browser_surface.dc;
    require(native_game::attach_render_imports(), "Cannot hook native GDI transfers");
    native_game::set_canvas_source(browser);
    const auto copy = [&] {
        require(BitBlt(output.dc, 0, 0, 64, 48, canvas.dc, 0, 0, SRCCOPY) != FALSE,
                "Native canvas copy failed");
    };
    copy();
    require(GetPixel(output.dc, 2, 2) == menu && calls == 0,
            "Offscreen copies must retain the native canvas");
    {
        const native_game::CanvasPresentation paint(canvas.dc);
        copy();
        require(GetPixel(output.dc, 2, 2) == overlay && GetPixel(output.dc, 62, 46) == overlay,
                "Window repaint restored native menu pixels over the browser");
        require(native_game::presentation_source(unrelated.dc) == unrelated.dc,
                "Unrelated native surface was replaced");
        {
            const native_game::CanvasPresentation nested(nullptr);
            copy();
            require(GetPixel(output.dc, 2, 2) == menu,
                    "Non-canvas transfer inherited an outer presentation scope");
        }
        const auto clip = CreateRectRgn(16, 12, 32, 24);
        SelectClipRgn(output.dc, clip);
        require(StretchBlt(output.dc, 0, 0, 64, 48, canvas.dc, 0, 0, 64, 48, SRCCOPY) != FALSE,
                "Clipped presentation failed");
        SelectClipRgn(output.dc, nullptr);
        DeleteObject(clip);
        require(GetPixel(output.dc, 20, 16) == overlay && GetPixel(output.dc, 2, 2) == menu,
                "Presentation lost the native destination clip or nested scope restoration");
        fail = true;
        copy();
        require(GetPixel(output.dc, 20, 16) == menu,
                "Failed browser callback did not fall back to the native canvas");
        fail = false;
        replacement = nullptr;
        copy();
        require(GetPixel(output.dc, 20, 16) == menu, "Null browser surface was not handled");
        replacement = browser_surface.dc;
        copy();
        require(GetPixel(output.dc, 2, 2) == overlay,
                "Browser did not recover after callback failure");
        native_game::set_canvas_source(nullptr);
        copy();
        require(GetPixel(output.dc, 2, 2) == menu, "Closing the browser retained its pixels");
        native_game::set_canvas_source(browser);
    }
    copy();
    require(GetPixel(output.dc, 2, 2) == menu && GetPixel(canvas.dc, 2, 2) == menu,
            "Repaint changed the native canvas or leaked its presentation scope");
    native_game::set_canvas_source(nullptr);
    native_game::detach_render_imports();
}
}

int main() {
    try {
        run();
        std::cout << "Canvas presentation and clipped native repaint passed\n";
        return 0;
    } catch (const std::exception& error) {
        native_game::set_canvas_source(nullptr);
        native_game::detach_render_imports();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
