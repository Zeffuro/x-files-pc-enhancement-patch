#include "display_monitors.h"
#include "platform/imports.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <iostream>
#include <stdexcept>

namespace {
ImportHooks imports;

const std::vector<MONITORINFO> monitors{
    {sizeof(MONITORINFO), {-1600, 0, 0, 900}, {-1600, 0, 0, 860}, 0},
    {sizeof(MONITORINFO), {0, 0, 1280, 1024}, {0, 0, 1280, 984}, MONITORINFOF_PRIMARY},
    {sizeof(MONITORINFO), {1280, 240, 3200, 1440}, {1280, 280, 3200, 1440}, 0},
    {sizeof(MONITORINFO), {0, -1200, 1600, 0}, {0, -1200, 1600, -40}, 0},
};

HMONITOR WINAPI nearest_monitor(HWND window, DWORD) {
    WINDOWINFO info{sizeof(info)};
    if (!GetWindowInfo(window, &info)) {
        return nullptr;
    }
    long long largest = -1;
    long long closest = std::numeric_limits<long long>::max();
    std::size_t selected = 1;
    for (std::size_t index = 0; index < monitors.size(); ++index) {
        const auto& bounds = monitors[index].rcMonitor;
        const auto width = std::max(0L, std::min(info.rcWindow.right, bounds.right) -
                                            std::max(info.rcWindow.left, bounds.left));
        const auto height = std::max(0L, std::min(info.rcWindow.bottom, bounds.bottom) -
                                             std::max(info.rcWindow.top, bounds.top));
        const auto area = static_cast<long long>(width) * height;
        const auto dx =
            std::max({0L, bounds.left - info.rcWindow.right, info.rcWindow.left - bounds.right});
        const auto dy =
            std::max({0L, bounds.top - info.rcWindow.bottom, info.rcWindow.top - bounds.bottom});
        const auto distance = static_cast<long long>(dx) * dx + static_cast<long long>(dy) * dy;
        if (area > largest || (area == 0 && largest == 0 && distance < closest)) {
            largest = area;
            closest = distance;
            selected = index;
        }
    }
    return reinterpret_cast<HMONITOR>(selected + 1);
}

BOOL WINAPI monitor_info(HMONITOR monitor, LPMONITORINFO info) {
    const auto index = reinterpret_cast<std::size_t>(monitor);
    if (!index || index > monitors.size() || !info || info->cbSize != sizeof(MONITORINFO)) {
        return FALSE;
    }
    *info = monitors[index - 1];
    return TRUE;
}

FARPROC resolve(const char* name) {
    if (std::strcmp(name, "MonitorFromWindow") == 0) {
        return reinterpret_cast<FARPROC>(nearest_monitor);
    }
    if (std::strcmp(name, "GetMonitorInfoA") == 0) {
        return reinterpret_cast<FARPROC>(monitor_info);
    }
    return nullptr;
}
}

std::vector<MONITORINFO> synthetic_display_monitors() {
    return monitors;
}

void install_display_monitors(HMODULE library) {
    if (!imports.install(library, "user32.dll", resolve) ||
        !imports.previous(reinterpret_cast<FARPROC>(nearest_monitor)) ||
        !imports.previous(reinterpret_cast<FARPROC>(monitor_info))) {
        imports.remove();
        throw std::runtime_error("Cannot install synthetic monitor queries in DirectDraw.");
    }
}

void remove_display_monitors() {
    imports.remove();
}

void test_monitor_restore_moves(HWND window, decltype(&SetWindowPos) set_window_pos,
                                void (*pump)()) {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    for (std::size_t source = 0; source < monitors.size(); ++source) {
        for (std::size_t destination = 0; destination < monitors.size(); ++destination) {
            if (source == destination) {
                continue;
            }
            for (bool borderless : {false, true}) {
                for (const SIZE size : {SIZE{640, 480}, SIZE{1280, 960}}) {
                    RECT outer{0, 0, size.cx, size.cy};
                    require(AdjustWindowRectEx(&outer, GetWindowLongW(window, GWL_STYLE), FALSE,
                                               GetWindowLongW(window, GWL_EXSTYLE)),
                            "Cannot measure the moved restore window.");
                    const auto width = outer.right - outer.left;
                    const auto height = outer.bottom - outer.top;
                    const auto& from = monitors[source].rcWork;
                    const auto& to = monitors[destination].rcWork;
                    const LONG x = std::max(from.left + 10, from.right - width - 10);
                    const LONG y = std::max(from.top + 10, from.bottom - height - 10);
                    require(set_window_pos(window, nullptr, x, y, width, height,
                                           SWP_NOZORDER | SWP_NOACTIVATE),
                            "Cannot place the moved restore window.");
                    pump();
                    WINDOWINFO original{sizeof(original)}, current{sizeof(current)};
                    require(GetWindowInfo(window, &original),
                            "Cannot capture the moved restore window.");
                    require(nearest_monitor(window, 0) == reinterpret_cast<HMONITOR>(source + 1),
                            "Moved restore fixture selected the wrong starting monitor.");
                    const UINT message = borderless ? WM_APP + 117 : WM_NCLBUTTONDBLCLK;
                    SendMessageW(window, message, borderless ? 1 : HTCAPTION, 0);
                    pump();
                    SendMessageW(window, WM_ENTERSIZEMOVE, 0, 0);
                    require(set_window_pos(window, nullptr, to.left + 10, to.top + 10, 0, 0,
                                           SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE),
                            "Cannot move the enlarged window to another monitor.");
                    SendMessageW(window, WM_EXITSIZEMOVE, 0, 0);
                    pump();
                    require(nearest_monitor(window, 0) ==
                                reinterpret_cast<HMONITOR>(destination + 1),
                            "Moved restore fixture selected the wrong destination monitor.");
                    SendMessageW(window, message, borderless ? 2 : HTCAPTION, 0);
                    pump();
                    const LONG expected_x =
                        std::max(to.left, std::min(x - from.left + to.left, to.right - width));
                    const LONG expected_y =
                        std::max(to.top, std::min(y - from.top + to.top, to.bottom - height));
                    require(GetWindowInfo(window, &current) &&
                                current.rcClient.right - current.rcClient.left == size.cx &&
                                current.rcClient.bottom - current.rcClient.top == size.cy,
                            "Moved monitor restore lost the previous client size.");
                    require(current.rcWindow.left == expected_x &&
                                current.rcWindow.top == expected_y &&
                                current.rcWindow.right == expected_x + width &&
                                current.rcWindow.bottom == expected_y + height,
                            "Moved monitor restore used old monitor coordinates or escaped its "
                            "work area.");
                    std::cout << "Moved restore: " << source << " -> " << destination << ' '
                              << (borderless ? "borderless " : "doubleclick ") << size.cx << 'x'
                              << size.cy << std::endl;
                }
            }
        }
    }
    const auto selected = nearest_monitor(window, 0);
    SendMessageW(window, WM_APP + 117, 1, 0);
    const auto size_message = RegisterWindowMessageW(L"XFilesEnhancement.WindowSize");
    require(SendMessageW(window, size_message, 800, 600) == MAKELONG(800, 600),
            "Cannot select a window size while borderless.");
    SendMessageW(window, WM_APP + 117, 2, 0);
    pump();
    WINDOWINFO current{sizeof(current)};
    require(GetWindowInfo(window, &current) &&
                current.rcClient.right - current.rcClient.left == 800 &&
                current.rcClient.bottom - current.rcClient.top == 600 &&
                nearest_monitor(window, 0) == selected,
            "Borderless restore lost a newly selected window size or monitor.");
}
