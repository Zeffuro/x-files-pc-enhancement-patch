#include "notification.h"
#include <algorithm>
#include <string>

namespace enhancements {
namespace {
constexpr wchar_t class_name[] = L"XFilesStatus";
HWND overlay = nullptr;
HMODULE module = nullptr;
ULONGLONG expires = 0;
std::wstring text;

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
        const auto font =
            CreateFontW(-std::max(12L, area.bottom * 3 / 5), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Typist");
        const auto previous = SelectObject(dc, font);
        SetTextColor(dc, RGB(155, 210, 225));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, previous);
        DeleteObject(font);
        EndPaint(window, &state);
        return 0;
    }
    return DefWindowProcW(window, message, value, data);
}
}

void notify_status(HWND owner, const wchar_t* message) {
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
            CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, class_name,
                            L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, module, nullptr);
        if (!overlay) {
            UnregisterClassW(class_name, module);
            return;
        }
    }
    text = message;
    expires = GetTickCount64() + 2000;
    update_notification(owner);
    InvalidateRect(overlay, nullptr, FALSE);
}

void update_notification(HWND owner) {
    if (!overlay) {
        return;
    }
    if (GetTickCount64() >= expires || IsIconic(owner)) {
        ShowWindow(overlay, SW_HIDE);
        return;
    }
    WINDOWINFO info{sizeof(info)};
    if (!GetWindowInfo(owner, &info)) {
        return;
    }
    const auto& client = info.rcClient;
    const auto scale =
        std::min((client.right - client.left) / 640.0, (client.bottom - client.top) / 480.0);
    const auto width = static_cast<int>(260 * scale);
    const auto height = static_cast<int>(22 * scale);
    if (auto batch = BeginDeferWindowPos(1)) {
        batch = DeferWindowPos(batch, overlay, HWND_TOP, (client.left + client.right - width) / 2,
                               client.bottom - height - static_cast<int>(4 * scale), width, height,
                               SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (batch) {
            EndDeferWindowPos(batch);
        }
    }
}

void release_notification() {
    if (overlay) {
        DestroyWindow(overlay);
        overlay = nullptr;
        UnregisterClassW(class_name, module);
    }
}
}
