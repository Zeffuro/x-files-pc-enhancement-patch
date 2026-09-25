#include "keyboard_view.h"

#include <algorithm>
#include <filesystem>

namespace enhancements {
namespace {
constexpr wchar_t class_name[] = L"XFilesTextPanel";
HWND overlay = nullptr;
HMODULE module = nullptr;
unsigned selection = 0;
bool save_theme = false;
RECT previous{};

void fill(HDC dc, RECT bounds, COLORREF color) {
    const auto brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

void label(HDC dc, RECT bounds, const char* value, COLORREF color, UINT align = DT_CENTER) {
    SetTextColor(dc, color);
    DrawTextA(dc, value, -1, &bounds, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

LRESULT CALLBACK paint(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT state{};
        const auto target = BeginPaint(window, &state);
        RECT bounds{};
        GetClientRect(window, &bounds);
        const auto dc = CreateCompatibleDC(target);
        const auto bitmap = CreateCompatibleBitmap(target, bounds.right, bounds.bottom);
        const auto old_bitmap = SelectObject(dc, bitmap);
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowExtEx(dc, 460, 123, nullptr);
        SetViewportExtEx(dc, bounds.right, bounds.bottom, nullptr);
        fill(dc, {0, 0, 460, 123}, RGB(0, 0, 0));
        const auto edge = save_theme ? RGB(31, 100, 128) : RGB(207, 190, 146);
        const auto outline = CreateSolidBrush(edge);
        const RECT frame{0, 0, 460, 123};
        FrameRect(dc, &frame, outline);
        DeleteObject(outline);
        SetBkMode(dc, TRANSPARENT);
        static const auto loaded = [] {
            std::wstring path(32768, L'\0');
            const auto length =
                GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (!length || length >= path.size()) {
                return std::array<bool, 2>{};
            }
            path.resize(length);
            const auto folder = std::filesystem::path(path).parent_path();
            return std::array<bool, 2>{
                AddFontResourceExW((folder / L"HCD.TTR").c_str(), FR_PRIVATE, nullptr) != 0,
                AddFontResourceExW((folder / L"DLG.TTR").c_str(), FR_PRIVATE, nullptr) != 0};
        }();
        const auto face = save_theme && loaded[1]    ? L"Schmutz ICG Cleaned"
                          : !save_theme && loaded[0] ? L"Typist"
                                                     : L"Courier New";
        const auto font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                      DEFAULT_PITCH, face);
        const auto old_font = SelectObject(dc, font);
        label(dc, {10, 1, 450, 17}, save_theme ? "SAVE NAME" : "KEYBOARD", edge, DT_LEFT);
        const auto keys = keyboard_bounds();
        constexpr const char* actions[]{"SPACE", "DELETE", "CLEAR", "DONE"};
        for (unsigned index = 0; index < keys.size(); ++index) {
            auto key = keys[index];
            OffsetRect(&key, -keyboard_panel.left, -keyboard_panel.top);
            const bool chosen = index == selection;
            fill(dc, key, chosen ? RGB(34, 131, 170) : RGB(8, 25, 34));
            const auto rim = CreateSolidBrush(chosen ? RGB(165, 230, 248) : RGB(29, 67, 82));
            FrameRect(dc, &key, rim);
            DeleteObject(rim);
            const char letter[]{index < keyboard_letter_count ? keyboard_letters[index] : ' ', 0};
            label(dc, key,
                  index < keyboard_letter_count ? letter : actions[index - keyboard_letter_count],
                  chosen ? RGB(255, 255, 236) : RGB(220, 215, 195));
        }
        const auto small = CreateFontW(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                       DEFAULT_PITCH, L"Arial");
        SelectObject(dc, small);
        label(dc, {10, 110, 450, 122}, "D-PAD: SELECT    A: TYPE    X: DELETE    Y / START: CLOSE",
              RGB(149, 185, 196));
        SelectObject(dc, old_font);
        DeleteObject(small);
        DeleteObject(font);
        SetMapMode(dc, MM_TEXT);
        BitBlt(target, 0, 0, bounds.right, bounds.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window, &state);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

std::array<RECT, keyboard_key_count> keyboard_bounds() {
    std::array<RECT, keyboard_key_count> result{};
    for (unsigned i = 0; i < result.size(); ++i) {
        const bool action = i >= keyboard_letter_count;
        const LONG column = action ? i - keyboard_letter_count : i % 10;
        const LONG row = action ? 4 : i / 10;
        const LONG step = action ? 110 : 44;
        result[i] = {keyboard_panel.left + 10 + column * step, keyboard_panel.top + 19 + row * 18,
                     keyboard_panel.left + 10 + (column + 1) * step - 4,
                     keyboard_panel.top + 19 + row * 18 + 16};
    }
    return result;
}

void show_keyboard(HWND owner, bool save_menu, unsigned selected) {
    WINDOWINFO info{sizeof(WINDOWINFO)};
    if (!GetWindowInfo(owner, &info)) {
        return;
    }
    const auto& client = info.rcClient;
    const double scale =
        std::min((client.right - client.left) / 640.0, (client.bottom - client.top) / 480.0);
    const auto x = client.left + (client.right - client.left - 640 * scale) / 2;
    const auto y = client.top + (client.bottom - client.top - 480 * scale) / 2;
    const RECT bounds{static_cast<LONG>(x + keyboard_panel.left * scale),
                      static_cast<LONG>(y + keyboard_panel.top * scale),
                      static_cast<LONG>(x + keyboard_panel.right * scale),
                      static_cast<LONG>(y + keyboard_panel.bottom * scale)};
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
        SetLayeredWindowAttributes(overlay, 0, 255, LWA_ALPHA);
    }
    const bool changed = save_theme != save_menu || selection != selected ||
                         !EqualRect(&previous, &bounds) || !IsWindowVisible(overlay);
    save_theme = save_menu;
    selection = selected;
    previous = bounds;
    if (changed) {
        if (auto placement = BeginDeferWindowPos(1)) {
            placement = DeferWindowPos(placement, overlay, nullptr, bounds.left, bounds.top,
                                       bounds.right - bounds.left, bounds.bottom - bounds.top,
                                       SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            if (placement) {
                EndDeferWindowPos(placement);
            }
        }
        InvalidateRect(overlay, nullptr, FALSE);
    }
}

void hide_keyboard() {
    if (overlay) {
        ShowWindow(overlay, SW_HIDE);
    }
}

void release_keyboard() {
    if (overlay) {
        DestroyWindow(overlay);
        overlay = nullptr;
        UnregisterClassW(class_name, module);
    }
}
}
