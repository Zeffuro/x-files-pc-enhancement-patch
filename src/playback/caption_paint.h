#pragma once
#include "settings.h"
#include "platform/game_fonts.h"
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace playback {
inline const wchar_t* caption_font(CaptionFont selected) {
    static const auto loaded = [] {
        std::array<bool, caption_fonts.size()> result{true};
        std::vector<wchar_t> path(32768);
        const auto length =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length && length < path.size()) {
            const auto folder = std::filesystem::path(path.data()).parent_path();
            for (std::size_t i = 1; i < caption_fonts.size(); ++i) {
                result[i] = platform::register_private_font(folder / caption_fonts[i].file);
            }
        }
        return result;
    }();
    const auto index = static_cast<std::size_t>(selected);
    return caption_fonts[index < loaded.size() && loaded[index] ? index : 0].name;
}

inline void caption_background(HDC dc, RECT rect, const CaptionStyle& style) {
    if (!style.background || !style.opacity) {
        return;
    }
    const auto source = CreateCompatibleDC(dc);
    const auto bitmap = CreateCompatibleBitmap(dc, 1, 1);
    if (!source || !bitmap) {
        if (source) {
            DeleteDC(source);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        return;
    }
    const auto old = SelectObject(source, bitmap);
    SetPixelV(source, 0, 0, style.background_color);
    const BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(style.opacity * 255 / 100), 0};
    AlphaBlend(dc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, source, 0,
               0, 1, 1, blend);
    SelectObject(source, old);
    DeleteObject(bitmap);
    DeleteDC(source);
}

inline RECT paint_caption(HDC dc, RECT area, const std::wstring& text, const CaptionStyle& style,
                          int reference_width) {
    const auto saved = SaveDC(dc);
    const int height = std::max(12, MulDiv(reference_width, 18, 600)) * style.scale / 100;
    const auto font = CreateFontW(-height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH, caption_font(style.font));
    if (!saved || !font) {
        if (saved) {
            RestoreDC(dc, saved);
        }
        if (font) {
            DeleteObject(font);
        }
        return {};
    }
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    auto measured = area;
    measured.left += 4;
    measured.right -= 4;
    constexpr UINT format = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX;
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &measured, format | DT_CALCRECT);
    const auto width = std::min(area.right - area.left, measured.right - measured.left + 8);
    RECT backdrop{
        area.left + (area.right - area.left - width) / 2,
        std::max(area.top,
                 area.bottom - std::max<LONG>(height, measured.bottom - measured.top) - 4),
        area.left + (area.right - area.left + width) / 2, area.bottom};
    if (!text.empty()) {
        caption_background(dc, backdrop, style);
    }
    auto layout = backdrop;
    InflateRect(&layout, -4, -2);
    auto shadow = layout;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &shadow, format);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &layout, format);
    RestoreDC(dc, saved);
    DeleteObject(font);
    return backdrop;
}
}
