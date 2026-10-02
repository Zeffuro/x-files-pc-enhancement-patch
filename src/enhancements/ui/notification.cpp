#include "notification.h"
#include "game_style.h"
#include "game/render/native_render.h"
#include "game/render/canvas_surface.h"
#include "saves/browser_text.h"
#include <memory>
#include <string>

namespace enhancements {
namespace {
HWND owner = nullptr;
ULONGLONG expires = 0;
std::wstring text;
std::unique_ptr<native_game::CanvasSurface> surface;
HFONT font = nullptr;

void redraw(HWND window) {
    native_game::invalidate_canvas();
    if (IsWindow(window)) {
        InvalidateRect(window, nullptr, FALSE);
    }
}

HDC paint(HDC background) {
    if (text.empty() || !surface || !font || GetTickCount64() >= expires || !IsWindow(owner) ||
        IsIconic(owner)) {
        return background;
    }
    const auto dc = surface->copy(background);
    if (!dc) {
        return background;
    }
    const auto previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    RECT area{11, 11, 631, 35};
    SetTextColor(dc, RGB(0, 0, 0));
    constexpr UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    DrawTextW(dc, text.c_str(), -1, &area, flags);
    OffsetRect(&area, -1, -1);
    SetTextColor(dc, game_highlight);
    DrawTextW(dc, text.c_str(), -1, &area, flags);
    SelectObject(dc, previous);
    return dc;
}
}

void notify_status(HWND window, const wchar_t* message) {
    if (!IsWindow(window) || !message || !*message) {
        return;
    }
    if (!surface) {
        try {
            surface = std::make_unique<native_game::CanvasSurface>();
        } catch (...) {
            return;
        }
    }
    if (!font) {
        std::wstring executable(32768, L'\0');
        const auto length =
            GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length && length < executable.size()) {
            executable.resize(length);
            font = saves::create_browser_font(std::filesystem::path(executable).parent_path(), -16);
        }
    }
    owner = window;
    text = message;
    expires = GetTickCount64() + 2000;
    native_game::set_canvas_status(paint);
    redraw(owner);
}

void update_notification(HWND window) {
    if (!text.empty() && (window != owner || !IsWindow(owner) || GetTickCount64() >= expires)) {
        native_game::set_canvas_status(nullptr);
        text.clear();
        redraw(owner);
    }
}

void release_notification() {
    native_game::set_canvas_status(nullptr);
    if (!text.empty()) {
        redraw(owner);
    }
    owner = nullptr;
    expires = 0;
    text.clear();
    surface.reset();
    if (font) {
        DeleteObject(font);
        font = nullptr;
    }
}
}
