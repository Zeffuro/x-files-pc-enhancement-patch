#include "graphics.h"

#include <ddraw.h>
#include <utility>
#include <windows.h>
#include <wrl/client.h>

namespace {

using Microsoft::WRL::ComPtr;

struct Graphics {
    ComPtr<IDirectDraw> draw;
    ComPtr<IDirectDrawSurface> primary;
    unsigned long surface_flags = 0;
};

thread_local Graphics graphics;

short __cdecl set_draw_object(IUnknown* object) {
    ComPtr<IDirectDraw> replacement;

    if (object && FAILED(object->QueryInterface(IID_IDirectDraw, &replacement))) {
        return -50;
    }

    graphics.primary.Reset();
    graphics.draw = std::move(replacement);

    return 0;
}

short __cdecl set_primary_surface(IUnknown* surface, unsigned long flags) {
    ComPtr<IDirectDrawSurface> replacement;

    if (surface && FAILED(surface->QueryInterface(IID_IDirectDrawSurface, &replacement))) {
        return -50;
    }

    graphics.primary = std::move(replacement);
    graphics.surface_flags = flags;

    return 0;
}

}

Entry graphics_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::QTSetDDObject, set_draw_object),
        bind_entry(Selector::QTSetDDPrimarySurface, set_primary_surface),
    };
    return find_entry(selector, entries);
}

void release_graphics() {
    graphics.primary.Reset();
    graphics.draw.Reset();
    graphics.surface_flags = 0;
}

void present_graphics(HWND window, HDC source, const quickdraw::Rect& bounds) {
    HDC destination = nullptr;
    if (graphics.primary) {
        if (FAILED(graphics.primary->GetDC(&destination))) {
            unsupported(Selector::CopyBits, "Cannot obtain the DirectDraw drawing context", 0);
        }
    } else {
        destination = GetDC(window);
    }
    const auto saved = destination ? SaveDC(destination) : 0;
    const auto clip = CreateRectRgn(0, 0, 0, 0);
    if (destination && clip && GetClipRgn(source, clip) == 1) {
        ExtSelectClipRgn(destination, clip, RGN_AND);
    }
    if (clip) {
        DeleteObject(clip);
    }
    const auto copied =
        destination && BitBlt(destination, bounds.left, bounds.top, bounds.right - bounds.left,
                              bounds.bottom - bounds.top, source, bounds.left, bounds.top, SRCCOPY);
    GdiFlush();
    if (saved) {
        RestoreDC(destination, saved);
    }
    if (graphics.primary) {
        graphics.primary->ReleaseDC(destination);
    } else if (destination) {
        ReleaseDC(window, destination);
    }
    if (!copied) {
        unsupported(Selector::CopyBits, "Cannot present the window drawing port", 0);
    }
}
