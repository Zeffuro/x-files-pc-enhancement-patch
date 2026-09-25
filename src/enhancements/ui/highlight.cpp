#include "enhancements/input_source.h"
#include "enhancements/game_resources.h"
#include "highlight.h"
#include "enhancements/game_ui.h"
#include "enhancements/focus.h"
#include "settings.h"
#include "enhancements/dialogue.h"

#include <algorithm>

namespace enhancements {
namespace {

constexpr wchar_t class_name[] = L"XFilesControllerFocus";
constexpr COLORREF transparent = RGB(255, 0, 255);
HWND overlay = nullptr;
HMODULE module = nullptr;
RECT previous{};

LRESULT CALLBACK paint(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT state{};
        const auto dc = BeginPaint(window, &state);
        RECT bounds{};
        GetClientRect(window, &bounds);
        const auto background = CreateSolidBrush(transparent);
        const auto accent = CreateSolidBrush(RGB(40, 200, 255));
        FillRect(dc, &bounds, background);
        FrameRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        InflateRect(&bounds, -1, -1);
        FrameRect(dc, &bounds, accent);
        InflateRect(&bounds, -1, -1);
        FrameRect(dc, &bounds, accent);
        DeleteObject(accent);
        DeleteObject(background);
        EndPaint(window, &state);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}

void update_highlight(HWND window, bool focused) {
    RECT target{};
    bool found = false;
    POINT cursor{};
    const auto mode = settings().focus_highlight;
    const bool enabled =
        mode == FocusHighlight::Always || (mode == FocusHighlight::Automatic && controller_active);
    if (focused && enabled && settings().gamepad && GetCursorPos(&cursor) &&
        ScreenToClient(window, &cursor)) {
        const auto modal = game::modal_buttons();
        if (!modal.empty()) {
            for (const auto& item : modal) {
                if (PtInRect(&item, cursor)) {
                    target = item;
                    found = true;
                    break;
                }
            }
        } else if (current_dialogue()) {
            for (const auto& item : conversation_evidence()) {
                if (PtInRect(&item, cursor)) {
                    target = item;
                    found = true;
                    break;
                }
            }
        } else if (!game::current_input()) {
            auto script = game::script_controls();
            const auto has = [&](unsigned resource) {
                return std::find(script.resources.begin(), script.resources.end(), resource) !=
                       script.resources.end();
            };
            if (has(resource::workstation_search)) {
                script.buttons.push_back({330, 249, 610, 269});
            }
            if (has(resource::workstation_media)) {
                script.buttons.push_back(workstation_media_field);
            }
            if (!script.acknowledgement_buttons.empty()) {
                script.buttons = script.acknowledgement_buttons;
            } else if (script.script_dialog) {
                script.buttons = script.dialog_buttons;
                script.buttons.insert(script.buttons.end(), script.dialog_fields.begin(),
                                      script.dialog_fields.end());
            }
            const auto area = [](const RECT& rect) {
                return (rect.right - rect.left) * (rect.bottom - rect.top);
            };
            for (const auto& item : script.buttons) {
                if (area(item) < 640 * 240 && PtInRect(&item, cursor) &&
                    (!found || area(item) < area(target))) {
                    target = item;
                    found = true;
                }
            }
            if (found && has(resource::options) && target.top >= 135 && target.bottom <= 440 &&
                target.bottom - target.top <= 24) {
                target.right = target.left < 160 ? 355 : 622;
            }
        }
    }
    if (!found) {
        if (overlay) {
            ShowWindow(overlay, SW_HIDE);
        }
        return;
    }
    WINDOWINFO info{sizeof(WINDOWINFO)};
    if (!GetWindowInfo(window, &info)) {
        return;
    }
    // WindowInfo keeps physical coordinates despite the game's 640x480 API hooks.
    const auto& client = info.rcClient;
    const double scale =
        std::min((client.right - client.left) / 640.0, (client.bottom - client.top) / 480.0);
    const auto x = client.left + (client.right - client.left - 640 * scale) / 2;
    const auto y = client.top + (client.bottom - client.top - 480 * scale) / 2;
    RECT bounds{static_cast<LONG>(x + target.left * scale) - 4,
                static_cast<LONG>(y + target.top * scale) - 4,
                static_cast<LONG>(x + target.right * scale) + 4,
                static_cast<LONG>(y + target.bottom * scale) + 4};
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
        overlay = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, class_name,
            L"", WS_POPUP, 0, 0, 1, 1, window, nullptr, module, nullptr);
        if (!overlay) {
            UnregisterClassW(class_name, module);
            return;
        }
        SetLayeredWindowAttributes(overlay, transparent, 255, LWA_COLORKEY);
    }
    if (!EqualRect(&bounds, &previous) || !IsWindowVisible(overlay)) {
        previous = bounds;
        // cnc-ddraw offsets SetWindowPos for popups; the deferred API keeps screen coordinates.
        if (auto placement = BeginDeferWindowPos(1)) {
            placement = DeferWindowPos(placement, overlay, HWND_TOPMOST, bounds.left, bounds.top,
                                       bounds.right - bounds.left, bounds.bottom - bounds.top,
                                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
            if (placement) {
                EndDeferWindowPos(placement);
            }
        }
        InvalidateRect(overlay, nullptr, FALSE);
    }
}

void release_highlight() {
    if (overlay) {
        DestroyWindow(overlay);
        overlay = nullptr;
        UnregisterClassW(class_name, module);
    }
}

}
