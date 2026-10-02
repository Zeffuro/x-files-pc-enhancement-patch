#include "game/render/native_render.h"
#include "game/render/render_internal.h"
#include "enhancements/game_ui.h"
#include "quickdraw/world.h"
#include "devtools/game_state.h"
#include "enhancements/ui/notification.h"
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

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

    Surface(COLORREF color, int width = 64, int height = 48) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        require(dc && bitmap, "Cannot create presentation fixture");
        previous = SelectObject(dc, bitmap);
        const RECT bounds{0, 0, width, height};
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
HDC decorated_surface = nullptr;
bool decoration_fails = false;

HDC decorate(HDC source) {
    require(source == replacement, "Decoration bypassed the active browser surface");
    require(native_game::presentation_source(source) == source,
            "Decoration recursively substituted its source");
    if (decoration_fails) {
        throw std::runtime_error("Unavailable decoration");
    }
    return decorated_surface;
}

HDC browser(HDC native) {
    ++calls;
    require(native_game::presentation_source(native) == native,
            "Browser callback recursively substituted itself");
    if (fail) {
        throw std::runtime_error("Unavailable browser surface");
    }
    return replacement;
}

HDC failing_targets(HDC) {
    throw std::runtime_error("Unavailable targets");
}

HDC status_after_failure(HDC source) {
    require(source == decorated_surface, "Failed target layer discarded the content surface");
    return replacement;
}

void native_ui() {
    constexpr auto scene = RGB(60, 90, 120), accent = RGB(40, 220, 180);
    Surface canvas(scene, 640, 480), output(0, 640, 480);
    const auto owner = CreateWindowExW(0, L"STATIC", L"Canvas owner", WS_OVERLAPPED, 0, 0, 640, 480,
                                       nullptr, nullptr, nullptr, nullptr);
    require(owner && GetForegroundWindow() != owner, "Cannot create unfocused canvas fixture");
    const auto copy = [&] {
        const native_game::CanvasPresentation paint(canvas.dc);
        require(BitBlt(output.dc, 0, 0, 640, 480, canvas.dc, 0, 0, SRCCOPY) != FALSE,
                "Native UI canvas copy failed");
    };
    devtools::show_hotspots(owner, true, {{100, 100, 160, 140}});
    copy();
    require(GetPixel(output.dc, 100, 100) == accent && GetPixel(output.dc, 101, 101) == accent &&
                GetPixel(output.dc, 102, 102) == scene && GetPixel(canvas.dc, 100, 100) == scene,
            "Unfocused targets failed to compose without altering native canvas");
    devtools::show_hotspots(owner, true, {{200, 200, 260, 240}});
    copy();
    require(GetPixel(output.dc, 100, 100) == scene && GetPixel(output.dc, 200, 200) == accent,
            "Moved target retained a stale rectangle");
    enhancements::notify_status(owner, L"Auto-Save complete");
    copy();
    unsigned text_pixels = 0, scene_pixels = 0;
    for (int y = 10; y < 36; ++y) {
        for (int x = 10; x < 631; ++x) {
            const auto pixel = GetPixel(output.dc, x, y);
            text_pixels += pixel != scene;
            scene_pixels += pixel == scene;
        }
    }
    require(text_pixels > 20 && scene_pixels > 10000 && GetPixel(output.dc, 200, 200) == accent &&
                GetPixel(canvas.dc, 320, 20) == scene,
            "Status did not preserve scene pixels and existing target composition");
    const auto start = GetTickCount64();
    for (unsigned frame = 0; frame < 100; ++frame) {
        copy();
    }
    std::cout << "100 native target/status presentations: " << GetTickCount64() - start << "ms\n";
    unsigned owned_windows = 0;
    std::pair<HWND, unsigned*> owner_count{owner, &owned_windows};
    EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
            auto* values = reinterpret_cast<std::pair<HWND, unsigned*>*>(data);
            if (GetWindow(window, GW_OWNER) == values->first) {
                wchar_t name[128]{};
                GetClassNameW(window, name, 128);
                if (std::wstring_view(name) == L"XFilesStatus" ||
                    std::wstring_view(name) == L"XFilesDeveloperHotspots") {
                    ++*values->second;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&owner_count));
    require(owned_windows == 0, "Native UI created a separate popup surface");
    devtools::show_hotspots(owner, false, {});
    copy();
    require(GetPixel(output.dc, 200, 200) == scene, "Disabled target retained native pixels");
    const auto deadline = GetTickCount64() + 2100;
    while (GetTickCount64() < deadline) {
        Sleep(10);
    }
    enhancements::update_notification(owner);
    copy();
    require(GetPixel(output.dc, 320, 20) == scene, "Expired status retained native pixels");
    for (int y = 10; y < 36; ++y) {
        for (int x = 10; x < 631; ++x) {
            require(GetPixel(output.dc, x, y) == scene, "Status expiration left glyph pixels");
        }
    }
    enhancements::notify_status(owner, nullptr);
    devtools::show_hotspots(owner, true, {{100, 100, 160, 140}});
    DestroyWindow(owner);
    copy();
    require(GetPixel(output.dc, 100, 100) == scene, "Destroyed owner retained target pixels");
    devtools::release_hotspots();
    enhancements::release_notification();
}

void run() {
    constexpr auto menu = RGB(180, 20, 10), overlay = RGB(10, 40, 200);
    Surface canvas(menu), output(0), browser_surface(overlay), unrelated(0);
    Surface transcript_surface(RGB(20, 180, 40));
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
        decorated_surface = transcript_surface.dc;
        native_game::set_canvas_overlay(decorate);
        copy();
        require(GetPixel(output.dc, 2, 2) == RGB(20, 180, 40),
                "Canvas decoration did not reach the native presentation");
        decoration_fails = true;
        copy();
        require(GetPixel(output.dc, 2, 2) == overlay,
                "Failed decoration erased the active save browser");
        decoration_fails = false;
        decorated_surface = nullptr;
        copy();
        require(GetPixel(output.dc, 2, 2) == overlay,
                "Null decoration erased the active save browser");
        decorated_surface = transcript_surface.dc;
        native_game::set_canvas_targets(failing_targets);
        native_game::set_canvas_status(status_after_failure);
        copy();
        require(GetPixel(output.dc, 2, 2) == overlay,
                "Failed target decoration prevented status composition");
        native_game::set_canvas_targets(nullptr);
        native_game::set_canvas_status(nullptr);
        native_game::set_canvas_overlay(nullptr);
        native_game::set_canvas_source(nullptr);
        copy();
        require(GetPixel(output.dc, 2, 2) == menu, "Closing the browser retained its pixels");
        native_game::set_canvas_source(browser);
    }
    copy();
    require(GetPixel(output.dc, 2, 2) == menu && GetPixel(canvas.dc, 2, 2) == menu,
            "Repaint changed the native canvas or leaked its presentation scope");
    native_game::set_canvas_source(nullptr);
    native_ui();
    native_game::detach_render_imports();
}
}

int main() {
    try {
        run();
        std::cout << "Canvas presentation and clipped native repaint passed\n";
        return 0;
    } catch (const std::exception& error) {
        devtools::release_hotspots();
        enhancements::release_notification();
        native_game::set_canvas_source(nullptr);
        native_game::detach_render_imports();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
