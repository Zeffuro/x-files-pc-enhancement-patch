#include "render_internal.h"
#include "caption_surface.h"
#include "platform/imports.h"
#include <cstring>

namespace native_game {
namespace {
ImportHooks graphics;
ImportHooks user;
decltype(&BitBlt) original_bit_blt = nullptr;
decltype(&StretchBlt) original_stretch_blt = nullptr;
decltype(&PatBlt) original_pat_blt = nullptr;
decltype(&DeleteObject) original_delete = nullptr;
decltype(&FillRect) original_fill = nullptr;
decltype(&StretchDIBits) original_dib = nullptr;

template <typename Action> void track(Action action) {
    try {
        action();
    } catch (...) {
        caption_surface::clear();
    }
}

BOOL WINAPI bit_blt(HDC destination, int x, int y, int width, int height, HDC source, int sx,
                    int sy, DWORD operation) {
    const auto input = presentation_source(source);
    const auto result =
        original_bit_blt(destination, x, y, width, height, input, sx, sy, operation);
    if (result) {
        track([&] {
            caption_surface::copy(destination, {x, y, x + width, y + height}, input,
                                  {sx, sy, sx + width, sy + height}, operation);
        });
    }
    return result;
}

BOOL WINAPI stretch_blt(HDC destination, int x, int y, int width, int height, HDC source, int sx,
                        int sy, int sw, int sh, DWORD operation) {
    const auto input = presentation_source(source);
    const auto result =
        original_stretch_blt(destination, x, y, width, height, input, sx, sy, sw, sh, operation);
    if (result) {
        track([&] {
            caption_surface::copy(destination, {x, y, x + width, y + height}, input,
                                  {sx, sy, sx + sw, sy + sh}, operation);
        });
    }
    return result;
}

BOOL WINAPI pat_blt(HDC dc, int x, int y, int width, int height, DWORD operation) {
    const auto result = original_pat_blt(dc, x, y, width, height, operation);
    if (result) {
        track([&] { caption_surface::paint(dc, {x, y, x + width, y + height}); });
    }
    return result;
}

BOOL WINAPI delete_object(HGDIOBJ object) {
    const auto result = original_delete(object);
    if (result) {
        caption_surface::forget(object);
    }
    return result;
}

int WINAPI fill_rect(HDC dc, const RECT* bounds, HBRUSH brush) {
    const auto result = original_fill(dc, bounds, brush);
    if (result && bounds) {
        track([&] { caption_surface::paint(dc, *bounds); });
    }
    return result;
}

int WINAPI stretch_dib(HDC dc, int x, int y, int width, int height, int sx, int sy, int sw, int sh,
                       const void* bits, const BITMAPINFO* format, UINT usage, DWORD operation) {
    const auto result =
        original_dib(dc, x, y, width, height, sx, sy, sw, sh, bits, format, usage, operation);
    if (result && result != GDI_ERROR) {
        track([&] { caption_surface::paint(dc, {x, y, x + width, y + height}); });
    }
    return result;
}

FARPROC resolve(const char* name) {
    struct Hook {
        const char* name;
        FARPROC function;
    };

    const Hook hooks[]{
        {"BitBlt", reinterpret_cast<FARPROC>(&bit_blt)},
        {"StretchBlt", reinterpret_cast<FARPROC>(&stretch_blt)},
        {"PatBlt", reinterpret_cast<FARPROC>(&pat_blt)},
        {"DeleteObject", reinterpret_cast<FARPROC>(&delete_object)},
        {"FillRect", reinterpret_cast<FARPROC>(&fill_rect)},
        {"StretchDIBits", reinterpret_cast<FARPROC>(&stretch_dib)},
    };
    for (const auto& hook : hooks) {
        if (std::strcmp(name, hook.name) == 0) {
            return hook.function;
        }
    }
    return nullptr;
}
}

bool attach_render_imports() {
    if (!graphics.install(GetModuleHandleW(nullptr), "GDI32.dll", resolve)) {
        return false;
    }
    original_bit_blt = reinterpret_cast<decltype(original_bit_blt)>(
        graphics.previous(reinterpret_cast<FARPROC>(&bit_blt)));
    original_stretch_blt = reinterpret_cast<decltype(original_stretch_blt)>(
        graphics.previous(reinterpret_cast<FARPROC>(&stretch_blt)));
    original_pat_blt = reinterpret_cast<decltype(original_pat_blt)>(
        graphics.previous(reinterpret_cast<FARPROC>(&pat_blt)));
    original_delete = reinterpret_cast<decltype(original_delete)>(
        graphics.previous(reinterpret_cast<FARPROC>(&delete_object)));
    original_dib = reinterpret_cast<decltype(original_dib)>(
        graphics.previous(reinterpret_cast<FARPROC>(&stretch_dib)));
    user.install(GetModuleHandleW(nullptr), "USER32.dll", resolve);
    original_fill = reinterpret_cast<decltype(original_fill)>(
        user.previous(reinterpret_cast<FARPROC>(&fill_rect)));
    if (!original_bit_blt || !original_stretch_blt || !original_delete) {
        detach_render_imports();
        return false;
    }
    return true;
}

void detach_render_imports() {
    user.remove();
    graphics.remove();
}
}
