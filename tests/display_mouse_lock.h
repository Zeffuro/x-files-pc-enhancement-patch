#pragma once

#include "platform/cursor.h"
#include "platform/tool_cursor.h"
#include "platform/windowed_mouse_lock.h"

#include <stdexcept>
#include <iostream>
#include <vector>
#include <windows.h>

struct DisplayMouseApis {
    decltype(&GetForegroundWindow) foreground;
    decltype(&GetCursorPos) cursor;
    decltype(&SetCursorPos) move_cursor;
    decltype(&ShowWindow) show;
};

inline void test_windowed_mouse_lock(HWND game, decltype(&GetClipCursor) clip,
                                     decltype(&SetWindowPos) position,
                                     const DisplayMouseApis& native, void (*pump)()) {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const auto [foreground, cursor, move_cursor, show] = native;
    require(foreground && cursor && move_cursor && show, "Cannot find the physical mouse APIs.");
    const auto initial = cursor_clip(clip);
    const auto previous = foreground();
    POINT old_cursor{};
    require(cursor(&old_cursor), "Cannot save the physical cursor.");
    HWND other =
        CreateWindowExW(0, L"STATIC", L"Mouse confinement focus test", WS_OVERLAPPEDWINDOW, 20, 20,
                        240, 120, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(other != nullptr, "Cannot create the focus test window.");

    struct Cleanup {
        HWND game;
        HWND other;
        HWND previous;
        POINT cursor;
        decltype(&SetCursorPos) move_cursor;

        ~Cleanup() {
            platform::windowed_mouse_lock(game, false);
            DestroyWindow(other);
            if (IsWindow(previous)) {
                SetForegroundWindow(previous);
            }
            move_cursor(cursor.x, cursor.y);
        }
    } cleanup{game, other, previous, old_cursor, move_cursor};

    const auto message = RegisterWindowMessageW(L"XFilesEnhancement.WindowedMouseLock");
    const auto display = RegisterWindowMessageW(L"XFilesEnhancement.DisplayMode");
    const auto size = RegisterWindowMessageW(L"XFilesEnhancement.WindowSize");
    require(SendMessageW(game, message, 0, 0) == 1, "Windowed confinement was not off by default.");
    SendMessageW(game, display, 1, 0);
    SendMessageW(game, size, 640, 480);
    show(game, SW_SHOW);
    const auto activate = [&](HWND window) {
        show(window, SW_SHOW);
        SetForegroundWindow(window);
        if (foreground() != window) {
            INPUT allow{};
            allow.type = INPUT_KEYBOARD;
            allow.ki.wVk = VK_MENU;
            SendInput(1, &allow, sizeof(INPUT));
            allow.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &allow, sizeof(INPUT));
            SetForegroundWindow(window);
        }
        pump();
        require(foreground() == window, "Cannot focus the owned confinement test window.");
    };
    const char* phase = "default";
    const auto free = [&] {
        pump();
        const auto current = cursor_clip(clip);
        if (current != initial) {
            std::cerr << phase << ": clip " << current.left << ',' << current.top << " to "
                      << current.right << ',' << current.bottom << "; expected " << initial.left
                      << ',' << initial.top << " to " << initial.right << ',' << initial.bottom
                      << '\n';
            throw std::runtime_error("Windowed cursor confinement was not released.");
        }
    };
    const auto confined = [&] {
        pump();
        WINDOWINFO info{sizeof(info)};
        require(GetWindowInfo(game, &info), "Cannot inspect the physical game client.");
        const auto current = cursor_clip(clip);
        if (current != CursorClip{info.rcClient.left, info.rcClient.top, info.rcClient.right,
                                  info.rcClient.bottom}) {
            std::cerr << phase << ": physical client " << info.rcClient.left << ','
                      << info.rcClient.top << " to " << info.rcClient.right << ','
                      << info.rcClient.bottom << "; clip " << current.left << ',' << current.top
                      << " to " << current.right << ',' << current.bottom << '\n';
            throw std::runtime_error("Cursor confinement differs from the physical game client.");
        }
    };
    activate(game);
    free();
    platform::windowed_mouse_lock(game, true);
    require(SendMessageW(game, message, 0, 0) == 2, "Windowed confinement was not enabled.");
    confined();
    WINDOWINFO client{sizeof(client)};
    require(GetWindowInfo(game, &client), "Cannot read the initial confinement bounds.");
    require(move_cursor(client.rcClient.left - 20, client.rcClient.top - 20),
            "Cannot move the physical cursor outside the game.");
    POINT limited{};
    require(cursor(&limited) && limited.x >= client.rcClient.left &&
                limited.y >= client.rcClient.top && limited.x < client.rcClient.right &&
                limited.y < client.rcClient.bottom,
            "The physical cursor escaped its confinement bounds.");
    RECT legacy{0, 0, 640, 480};
    require(ClipCursor(&legacy), "The legacy game cursor request failed.");
    confined();

    std::vector<MONITORINFO> monitors;
    require(EnumDisplayMonitors(
                nullptr, nullptr,
                [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
                    MONITORINFO info{sizeof(info)};
                    if (!GetMonitorInfoW(monitor, &info)) {
                        return FALSE;
                    }
                    reinterpret_cast<std::vector<MONITORINFO>*>(data)->push_back(info);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&monitors)),
            "Cannot enumerate the confinement test monitors.");
    for (const auto& monitor : monitors) {
        phase = "monitor move and resize";
        RECT bounds{0, 0, 640, 480};
        require(AdjustWindowRectEx(&bounds, GetWindowLongW(game, GWL_STYLE), FALSE,
                                   GetWindowLongW(game, GWL_EXSTYLE)),
                "Cannot measure the confinement window border.");
        require(position(game, nullptr, monitor.rcWork.left + 10, monitor.rcWork.top + 10,
                         bounds.right - bounds.left, bounds.bottom - bounds.top,
                         SWP_NOZORDER | SWP_NOACTIVATE),
                "Cannot move the confinement test window.");
        confined();
        SendMessageW(game, WM_ENTERSIZEMOVE, 0, 0);
        free();
        SendMessageW(game, WM_EXITSIZEMOVE, 0, 0);
        confined();
    }

    phase = "wide physical client";
    SendMessageW(game, size, 800, 480);
    confined();
    require(ClipCursor(nullptr), "The legacy cursor release request failed.");
    confined();

    phase = "pause";
    platform::windowed_mouse_lock(game, true, true);
    free();
    for (int repeat = 0; repeat < 5; ++repeat) {
        platform::windowed_mouse_lock(game, true, true);
    }
    free();
    platform::windowed_mouse_lock(game, true, false);
    confined();
    phase = "nested tools";
    {
        platform::ToolCursor outer(game);
        free();
        {
            platform::ToolCursor inner(game);
            free();
        }
        free();
        platform::windowed_mouse_lock(game, true);
        free();
    }
    confined();
    phase = "focus loss";
    activate(other);
    free();
    platform::windowed_mouse_lock(game, true);
    free();
    activate(game);
    confined();
    phase = "minimize";
    show(game, SW_MINIMIZE);
    free();
    show(game, SW_RESTORE);
    activate(game);
    confined();

    // Exercise the wrapper's existing release shortcut through its keyboard hook.
    phase = "release shortcut";
    INPUT keys[2]{};
    keys[0].type = keys[1].type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_CONTROL;
    keys[1].ki.wVk = VK_TAB;
    {
        struct ReleaseKeys {
            INPUT* keys;

            ~ReleaseKeys() {
                keys[0].ki.dwFlags = keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, keys, sizeof(INPUT));
            }
        } release{keys};

        require(SendInput(2, keys, sizeof(INPUT)) == 2,
                "Cannot press the cursor release shortcut.");
        pump();
    }
    free();
    phase = "release persistence";
    platform::windowed_mouse_lock(game, true);
    SendMessageW(game, size, 640, 480);
    SendMessageW(game, WM_MOVE, 0, 0);
    free();
    platform::windowed_mouse_lock(game, true, true);
    platform::windowed_mouse_lock(game, true, false);
    free();
    phase = "click relock";
    SendMessageW(game, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 100));
    SendMessageW(game, WM_LBUTTONUP, 0, MAKELPARAM(100, 100));
    confined();
    phase = "borderless";
    SendMessageW(game, display, 2, 0);
    free();
    SendMessageW(game, display, 1, 0);
    SendMessageW(game, WM_LBUTTONUP, 0, MAKELPARAM(100, 100));
    confined();
    platform::windowed_mouse_lock(game, false);
    free();
}
