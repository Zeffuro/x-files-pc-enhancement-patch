#include "menu_link.h"
#include "enhancements/game_ui.h"

#include <algorithm>

namespace enhancements {
namespace {
constexpr wchar_t class_name[] = L"XFilesEnhancementMenu";
HWND overlay = nullptr;
HMODULE module = nullptr;
HBITMAP artwork = nullptr;
int frame = 0;
ULONGLONG last_frame = 0;
RECT previous{};
constexpr auto artwork_bounds = settings_link;
constexpr int frame_width = 336, frame_height = 86, last = 20;

LRESULT CALLBACK paint(HWND window, UINT message, WPARAM value, LPARAM data) {
    if (message == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT state{};
        const auto dc = BeginPaint(window, &state);
        RECT area{};
        GetClientRect(window, &area);
        FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (artwork) {
            const auto source = CreateCompatibleDC(dc);
            const auto previous_bitmap = SelectObject(source, artwork);
            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, nullptr);
            StretchBlt(dc, 0, 0, area.right, area.bottom, source, 0, frame * frame_height,
                       frame_width, frame_height, SRCCOPY);
            SelectObject(source, previous_bitmap);
            DeleteDC(source);
        }
        EndPaint(window, &state);
        return 0;
    }
    return DefWindowProcW(window, message, value, data);
}
}

bool settings_link_visible() {
    return game::input_vtable() == game::edition().main_menu && !game::menu_confirmation_active();
}

void update_settings_link(HWND owner, bool visible) {
    if (!visible || !settings_link_visible()) {
        if (overlay) {
            ShowWindow(overlay, SW_HIDE);
        }
        frame = 0;
        last_frame = GetTickCount64();
        return;
    }
    WINDOWINFO info{sizeof(info)};
    if (!GetWindowInfo(owner, &info)) {
        return;
    }
    if (!overlay) {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&paint), &module)) {
            return;
        }
        WNDCLASSW type{};
        type.hInstance = module;
        type.lpfnWndProc = paint;
        type.lpszClassName = class_name;
        if (!RegisterClassW(&type)) {
            return;
        }
        overlay =
            CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            class_name, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, module, nullptr);
        if (!overlay) {
            UnregisterClassW(class_name, module);
            return;
        }
        SetLayeredWindowAttributes(overlay, RGB(0, 0, 0), 255, LWA_COLORKEY);
        artwork = LoadBitmapW(module, MAKEINTRESOURCEW(201));
    }
    const auto& client = info.rcClient;
    const auto scale =
        std::min((client.right - client.left) / 640.0, (client.bottom - client.top) / 480.0);
    const auto x = client.left + (client.right - client.left - 640 * scale) / 2;
    const auto y = client.top + (client.bottom - client.top - 480 * scale) / 2;
    RECT bounds{static_cast<LONG>(x + artwork_bounds.left * scale),
                static_cast<LONG>(y + artwork_bounds.top * scale),
                static_cast<LONG>(x + artwork_bounds.right * scale),
                static_cast<LONG>(y + artwork_bounds.bottom * scale)};
    POINT cursor{};
    const bool hot =
        GetCursorPos(&cursor) && ScreenToClient(owner, &cursor) && PtInRect(&settings_link, cursor);
    if (!EqualRect(&bounds, &previous) || !IsWindowVisible(overlay)) {
        previous = bounds;
        if (auto positions = BeginDeferWindowPos(1)) {
            positions = DeferWindowPos(positions, overlay, nullptr, bounds.left, bounds.top,
                                       bounds.right - bounds.left, bounds.bottom - bounds.top,
                                       SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            if (positions) {
                EndDeferWindowPos(positions);
            }
        }
        InvalidateRect(overlay, nullptr, FALSE);
    }
    const auto now = GetTickCount64();
    const auto elapsed = now - last_frame;
    const auto steps = static_cast<int>(std::min<ULONGLONG>(elapsed / 20, last));
    const auto next = std::clamp(frame + (hot ? steps : -steps), 0, last);
    if (steps) {
        last_frame = now;
    }
    if (next != frame) {
        frame = next;
        InvalidateRect(overlay, nullptr, FALSE);
    }
}

void release_settings_link() {
    if (overlay) {
        DestroyWindow(overlay);
        overlay = nullptr;
        UnregisterClassW(class_name, module);
    }
    if (artwork) {
        DeleteObject(artwork);
        artwork = nullptr;
    }
}
}
