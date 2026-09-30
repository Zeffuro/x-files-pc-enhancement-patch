#include "controller_hints.h"
#include "game_style.h"
#include "quick_menu.h"
#include "enhancements/input_source.h"
#include "enhancements/game_ui.h"
#include "enhancements/dialogue.h"
#include "enhancements/inventory.h"
#include "settings.h"
#include <algorithm>
#include <array>

namespace enhancements {
namespace {
constexpr wchar_t class_name[] = L"XFilesControllerHints";
thread_local HWND overlay = nullptr;
thread_local HMODULE module = nullptr;

struct Hint {
    const wchar_t* label = nullptr;
    controller::Binding binding = controller::Binding::A;
    bool operator==(const Hint&) const = default;
};

thread_local std::array<Hint, 6> labels{};

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    if (message == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, game_blue);
        const int height = bounds.bottom, radius = std::max(1, height / 9);
        const auto font = create_game_font(-std::max(10, height * 3 / 4));
        const auto old_font = SelectObject(dc, font);
        const auto accent = CreateSolidBrush(game_highlight);
        const auto outline = CreatePen(PS_SOLID, 1, RGB(35, 67, 80));
        const auto old_pen = SelectObject(dc, outline);
        const auto old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        const int count = static_cast<int>(std::count_if(
            labels.begin(), labels.end(), [](const auto& hint) { return hint.label != nullptr; }));
        int index = 0;
        for (unsigned selected = 0; selected < labels.size(); ++selected) {
            if (!labels[selected].label) {
                continue;
            }
            const int x = 2 + index++ * bounds.right / std::max(1, count);
            const int center = x + height / 2;
            const int step = height / 4;
            const std::array<POINT, 4> points{{{center, height / 2 + step},
                                               {center + step, height / 2},
                                               {center - step, height / 2},
                                               {center, height / 2 - step}}};
            const auto binding = static_cast<unsigned>(labels[selected].binding);
            for (unsigned button = 0; binding < points.size() && button < points.size(); ++button) {
                const auto point = points[button];
                SelectObject(dc, button == binding ? accent : GetStockObject(HOLLOW_BRUSH));
                Ellipse(dc, point.x - radius, point.y - radius, point.x + radius + 1,
                        point.y + radius + 1);
            }
            if (binding >= points.size()) {
                RECT badge{x, 0, x + height * 2, height};
                SetTextColor(dc, game_highlight);
                DrawTextW(dc, controller::binding_names[binding], -1, &badge,
                          DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
                SetTextColor(dc, game_blue);
            }
            RECT text{x + height * (binding < points.size() ? 1 : 2) + 3, 0,
                      x + bounds.right / std::max(1, count) - 4, height};
            DrawTextW(dc, labels[selected].label, -1, &text,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        SelectObject(dc, old_font);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(font);
        DeleteObject(accent);
        DeleteObject(outline);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, parameter, data);
}
}

void update_controller_hints(HWND window, bool enabled) {
    std::array<const wchar_t*, 6> actions{};
    RECT dialogue_panel{};
    if (enabled && settings().controller_hints && settings().gamepad && controller_active) {
        if (const auto buttons = game::modal_buttons(); !buttons.empty()) {
            actions = {L"Select", nullptr, nullptr, buttons.size() == 1 ? L"Close" : nullptr};
        } else if (game::input_vtable() == game::edition().main_menu) {
            actions = {L"Select", nullptr, nullptr, nullptr};
        } else if (quick_menu::expanded()) {
            actions = {L"Select", nullptr, nullptr, L"Close"};
        } else if (inventory_focused(window)) {
            actions = {L"Use item", nullptr, L"Examine", L"Back"};
        } else if (const auto dialogue = current_dialogue()) {
            actions = {L"Select", L"Close", nullptr, L"Back"};
            dialogue_panel = dialogue->panel;
        } else if (game::world_navigation_available() && game::emotion_targets().empty() &&
                   game::script_controls().buttons.empty()) {
            actions = {L"Interact", L"Inventory", L"Examine", nullptr, L"Aim", L"Targets"};
        }
    }
    std::array<Hint, 6> next{};
    constexpr std::array hint_actions{controller::Action::Activate, controller::Action::Inventory,
                                      controller::Action::Examine,  controller::Action::Back,
                                      controller::Action::Aim,      controller::Action::Targets};
    for (std::size_t index = 0; index < next.size(); ++index) {
        next[index] = {
            actions[index],
            settings().controller_profile.bindings[static_cast<std::size_t>(hint_actions[index])]};
    }
    if (actions == std::array<const wchar_t*, 6>{}) {
        if (overlay) {
            ShowWindow(overlay, SW_HIDE);
        }
        return;
    }
    WINDOWINFO info{sizeof(WINDOWINFO)};
    if (!GetWindowInfo(window, &info)) {
        return;
    }
    if (!overlay) {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&procedure), &module)) {
            return;
        }
        WNDCLASSW type{};
        type.hInstance = module;
        type.lpfnWndProc = procedure;
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
        SetLayeredWindowAttributes(overlay, 0, 255, LWA_ALPHA);
    }
    const auto& client = info.rcClient;
    const auto scale =
        std::min((client.right - client.left) / 640.0, (client.bottom - client.top) / 480.0);
    const auto count = std::count_if(next.begin(), next.end(),
                                     [](const auto& hint) { return hint.label != nullptr; });
    const auto height = std::max(14, static_cast<int>(14 * scale));
    auto width = std::min(client.right - client.left, static_cast<LONG>(count * height * 7));
    int x = client.left + (client.right - client.left - width) / 2;
    const bool menu = game::input_vtable() == game::edition().main_menu;
    const auto top = client.top + (client.bottom - client.top - 480 * scale) / 2;
    int y = static_cast<int>(top + (menu ? 457 : 406) * scale);
    if (!IsRectEmpty(&dialogue_panel)) {
        const auto left = client.left + (client.right - client.left - 640 * scale) / 2;
        x = static_cast<int>(left + dialogue_panel.left * scale);
        width = static_cast<LONG>((dialogue_panel.right - dialogue_panel.left) * scale);
        y = static_cast<int>(top + (dialogue_panel.bottom + 1) * scale);
    }
    RECT old{};
    GetWindowRect(overlay, &old);
    const bool changed = labels != next || old.left != x || old.top != y ||
                         old.right - old.left != width || old.bottom - old.top != height;
    labels = next;
    if (changed || !IsWindowVisible(overlay)) {
        if (auto placement = BeginDeferWindowPos(1)) {
            placement = DeferWindowPos(placement, overlay, HWND_TOPMOST, x, y, width, height,
                                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
            if (placement) {
                EndDeferWindowPos(placement);
            }
        }
        InvalidateRect(overlay, nullptr, FALSE);
    }
}

void release_controller_hints() {
    if (overlay) {
        DestroyWindow(overlay);
        overlay = nullptr;
        UnregisterClassW(class_name, module);
    }
}
}
