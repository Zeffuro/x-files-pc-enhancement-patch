#pragma once

#include <windows.h>
#include <filesystem>
#include <string>
#include "platform/game_fonts.h"

namespace enhancements {
inline HFONT create_game_font(int height) {
    static const bool native = platform::register_game_font(L"HCD.TTR");
    return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                       native ? L"Typist" : L"Courier New");
}

inline constexpr COLORREF game_blue = RGB(51, 129, 161);
inline constexpr COLORREF game_highlight = RGB(155, 216, 239);
}
