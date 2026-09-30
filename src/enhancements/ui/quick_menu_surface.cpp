#include "quick_menu_surface.h"
#include "menu_icons.h"
#include "game_style.h"
#include <algorithm>
#include <stdexcept>

namespace enhancements::quick_menu {
namespace {
HBITMAP bitmap(HDC dc, int width, int height, void** pixels) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    return CreateDIBSection(dc, &info, DIB_RGB_COLORS, pixels, nullptr, 0);
}

void fill(HDC dc, RECT bounds, COLORREF color) {
    const auto brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}
}

Surface::Surface() {
    dc_ = CreateCompatibleDC(nullptr);
    icon_dc_ = CreateCompatibleDC(nullptr);
    void* unused = nullptr;
    bitmap_ = bitmap(dc_, 640, 480, &unused);
    icon_bitmap_ =
        bitmap(icon_dc_, menu_icons::size, menu_icons::size, reinterpret_cast<void**>(&pixels_));
    if (!dc_ || !icon_dc_ || !bitmap_ || !icon_bitmap_) {
        if (bitmap_) {
            DeleteObject(bitmap_);
        }
        if (icon_bitmap_) {
            DeleteObject(icon_bitmap_);
        }
        if (dc_) {
            DeleteDC(dc_);
        }
        if (icon_dc_) {
            DeleteDC(icon_dc_);
        }
        throw std::runtime_error("Cannot create game menu");
    }
    previous_ = SelectObject(dc_, bitmap_);
    icon_previous_ = SelectObject(icon_dc_, icon_bitmap_);
    font_ =
        CreateFontW(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
}

Surface::~Surface() {
    SelectObject(dc_, previous_);
    SelectObject(icon_dc_, icon_previous_);
    DeleteObject(font_);
    DeleteObject(bitmap_);
    DeleteObject(icon_bitmap_);
    DeleteDC(dc_);
    DeleteDC(icon_dc_);
}

RECT Surface::toggle(RECT native_button) {
    return {native_button.right - 28, native_button.top, native_button.right,
            native_button.top + 28};
}

RECT Surface::button(RECT native_button, unsigned index, const std::array<bool, 5>& visible) {
    auto bounds = toggle(native_button);
    const auto remaining = std::count(visible.begin() + index, visible.end(), true);
    OffsetRect(&bounds, -static_cast<int>(remaining) * 28, 0);
    return bounds;
}

RECT Surface::reveal(RECT native_button) {
    return {native_button.right - 32, native_button.top, native_button.right,
            native_button.top + 32};
}

RECT Surface::region(RECT native_button, const std::array<bool, 5>& visible) {
    auto bounds = toggle(native_button);
    bounds.left -= static_cast<LONG>(std::count(visible.begin(), visible.end(), true)) * 28 + 4;
    bounds.right = std::max(bounds.right, native_button.right);
    bounds.bottom += 22;
    return bounds;
}

void Surface::icon(unsigned index, int x, int y, COLORREF color) {
    using Mask = std::array<std::uint8_t, menu_icons::size * menu_icons::size>;
    const std::array<const Mask*, 6> masks{&menu_icons::floppy_disk, &menu_icons::folder_open,
                                           &menu_icons::comment,     &menu_icons::gear,
                                           &menu_icons::x_mark,      &menu_icons::bars};
    for (std::size_t i = 0; i < masks[index]->size(); ++i) {
        const auto alpha = static_cast<unsigned>((*masks[index])[i]);
        pixels_[i] = (alpha << 24) | (GetRValue(color) * alpha / 255 << 16) |
                     (GetGValue(color) * alpha / 255 << 8) | GetBValue(color) * alpha / 255;
    }
    AlphaBlend(dc_, x, y, menu_icons::size, menu_icons::size, icon_dc_, 0, 0, menu_icons::size,
               menu_icons::size, {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA});
}

HDC Surface::draw(HDC background, RECT native_button, int progress, int hover,
                  const std::array<bool, 5>& enabled, const std::array<bool, 5>& visible) {
    BitBlt(dc_, 0, 0, 640, 480, background, 0, 0, SRCCOPY);
    fill(dc_, native_button, RGB(0, 0, 0));
    const auto anchor = toggle(native_button);
    const auto width = static_cast<int>(std::count(visible.begin(), visible.end(), true)) * 28 + 4;
    const auto extent = progress * width / 100;
    if (extent) {
        RECT panel{anchor.left - extent, anchor.top, anchor.right, anchor.bottom};
        fill(dc_, panel, RGB(0, 0, 0));
        const auto brush = CreateSolidBrush(RGB(23, 66, 89));
        FrameRect(dc_, &panel, brush);
        DeleteObject(brush);
        const auto saved = SaveDC(dc_);
        IntersectClipRect(dc_, panel.left, panel.top, anchor.left, panel.bottom);
        for (unsigned i = 0; i < 5; ++i) {
            if (!visible[i]) {
                continue;
            }
            const auto bounds = button(native_button, i, visible);
            icon(i, bounds.left + 5 + width - extent, bounds.top + 5,
                 !enabled[i]                    ? RGB(46, 66, 76)
                 : hover == static_cast<int>(i) ? game_highlight
                                                : game_blue);
        }
        RestoreDC(dc_, saved);
    }
    icon(5, anchor.left + 5, anchor.top + 5, hover == 5 ? game_highlight : game_blue);
    if (progress == 100 && hover >= 0 && hover < 5) {
        const std::array labels{L"Save", L"Load", L"Transcript", L"Tweaks", L"Menu"};
        const auto old = SelectObject(dc_, font_);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, game_highlight);
        const auto item = button(native_button, hover, visible);
        RECT label{std::max<LONG>(0, item.left - 35), item.bottom + 2,
                   std::min<LONG>(640, item.right + 35), item.bottom + 22};
        DrawTextW(dc_, labels[hover], -1, &label, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc_, old);
    }
    return dc_;
}
}
