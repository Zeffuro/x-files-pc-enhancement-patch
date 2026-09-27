#include "game_state.h"
#include "enhancements/controls.h"
#include <commctrl.h>
#include <algorithm>

namespace devtools {
namespace {
constexpr wchar_t class_name[] = L"XFilesDeveloperHotspots";
constexpr COLORREF transparent = RGB(255, 0, 255);
HWND overlay = nullptr;
HWND owner = nullptr;
HMODULE module = nullptr;
std::vector<RECT> rectangles;

void position(HWND game) {
    WINDOWINFO info{sizeof(info)};
    if (!overlay || !GetWindowInfo(game, &info)) {
        return;
    }
    const auto& r = info.rcClient;
    // The deferred API bypasses cnc-ddraw's popup-coordinate adjustment.
    if (auto placement = BeginDeferWindowPos(1)) {
        placement = DeferWindowPos(placement, overlay, HWND_TOP, r.left, r.top, r.right - r.left,
                                   r.bottom - r.top, SWP_NOACTIVATE);
        if (placement) {
            EndDeferWindowPos(placement);
        }
    }
}

LRESULT CALLBACK follow_owner(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR id,
                              DWORD_PTR) {
    const auto result = DefSubclassProc(window, message, value, data);
    if (message == WM_WINDOWPOSCHANGED) {
        position(window);
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, follow_owner, id);
        owner = nullptr;
        overlay = nullptr;
        rectangles.clear();
        UnregisterClassW(class_name, module);
    }
    return result;
}

LRESULT CALLBACK paint(HWND window, UINT message, WPARAM value, LPARAM data) {
    if (message == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (message == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        const auto dc = BeginPaint(window, &ps);
        RECT bounds{};
        GetClientRect(window, &bounds);
        const auto background = CreateSolidBrush(transparent);
        const auto accent = CreateSolidBrush(RGB(40, 220, 180));
        FillRect(dc, &bounds, background);
        const double scale = std::min(bounds.right / 640.0, bounds.bottom / 480.0);
        const double x = (bounds.right - 640 * scale) / 2, y = (bounds.bottom - 480 * scale) / 2;
        for (const auto& rect : rectangles) {
            RECT target{static_cast<LONG>(x + rect.left * scale),
                        static_cast<LONG>(y + rect.top * scale),
                        static_cast<LONG>(x + rect.right * scale),
                        static_cast<LONG>(y + rect.bottom * scale)};
            FrameRect(dc, &target, accent);
            InflateRect(&target, -1, -1);
            FrameRect(dc, &target, accent);
        }
        DeleteObject(background);
        DeleteObject(accent);
        EndPaint(window, &ps);
        return 0;
    }
    return DefWindowProcW(window, message, value, data);
}
}

void show_hotspots(HWND game, bool enabled, const std::vector<RECT>& targets) {
    if (!enabled || targets.empty() || IsIconic(game) || !enhancements::game_is_foreground(game)) {
        if (overlay) {
            ShowWindow(overlay, SW_HIDE);
        }
        return;
    }
    if (!overlay) {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(paint), &module);
        WNDCLASSW type{};
        type.hInstance = module;
        type.lpfnWndProc = paint;
        type.lpszClassName = class_name;
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        overlay =
            CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            class_name, L"", WS_POPUP, 0, 0, 1, 1, game, nullptr, module, nullptr);
        if (!overlay) {
            UnregisterClassW(class_name, module);
            return;
        }
        SetLayeredWindowAttributes(overlay, transparent, 255, LWA_COLORKEY);
        if (!SetWindowSubclass(game, follow_owner, reinterpret_cast<UINT_PTR>(follow_owner), 0)) {
            release_hotspots();
            return;
        }
        owner = game;
    }
    rectangles = targets;
    position(game);
    ShowWindow(overlay, SW_SHOWNOACTIVATE);
    InvalidateRect(overlay, nullptr, FALSE);
}

void release_hotspots() {
    if (owner) {
        RemoveWindowSubclass(owner, follow_owner, reinterpret_cast<UINT_PTR>(follow_owner));
        owner = nullptr;
    }
    if (overlay) {
        DestroyWindow(overlay);
        overlay = nullptr;
    }
    if (module) {
        UnregisterClassW(class_name, module);
    }
    rectangles.clear();
}
}
