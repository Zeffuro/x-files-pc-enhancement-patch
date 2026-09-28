#include "ddraw_version.h"
#include "graphics.h"
#include "identity.h"
#include "platform/desktop.h"
#include "platform/cursor.h"

#include <ddraw.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <windows.h>
#include <wrl/client.h>

namespace {

using Microsoft::WRL::ComPtr;
using CreateDraw = HRESULT(WINAPI*)(GUID*, IDirectDraw**, IUnknown*);
using BindDraw = short(__cdecl*)(IUnknown*);
using BindSurface = short(__cdecl*)(IUnknown*, unsigned long);

CursorClip initial_clip;
decltype(&GetClipCursor) read_cursor_clip;
decltype(&ShowCursor) real_show_cursor;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void pump() {
    const auto until = GetTickCount64() + 150;
    do {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    } while (GetTickCount64() < until);
}

void check_desktop(const Desktop& expected) {
    pump();
    const auto current = desktop_layout();
    if (current != expected) {
        write_desktop(std::cerr, current);
        throw std::runtime_error("The physical monitor layout changed.");
    }
    const auto clip = cursor_clip(read_cursor_clip);
    if (clip != initial_clip) {
        std::cerr << "Cursor clip: " << clip.left << ',' << clip.top << " to " << clip.right << ','
                  << clip.bottom << "; expected " << initial_clip.left << ',' << initial_clip.top
                  << " to " << initial_clip.right << ',' << initial_clip.bottom << '\n';
        throw std::runtime_error("Cursor confinement changed during the display test.");
    }
}

void dvd_frame(HWND window, IDirectDrawSurface* primary, DWORD width, DWORD height) {
    const auto message = RegisterWindowMessageW(L"XFilesEnhancement.DvdFrame");
    const auto dimensions = MAKELONG(width, height);
    require(SendMessageW(window, message, 0, 0) == dimensions,
            "DVD output did not report the primary surface size.");

    struct Canvas {
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previous = nullptr;

        ~Canvas() {
            if (previous) {
                SelectObject(dc, previous);
            }
            if (bitmap) {
                DeleteObject(bitmap);
            }
            if (dc) {
                DeleteDC(dc);
            }
        }
    } canvas;

    canvas.dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    canvas.bitmap = CreateDIBSection(canvas.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(canvas.dc && canvas.bitmap && pixels, "Cannot create the DVD frame buffer.");
    canvas.previous = SelectObject(canvas.dc, canvas.bitmap);
    require(canvas.previous && canvas.previous != HGDI_ERROR, "Cannot select the DVD bitmap.");
    RECT bounds{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    require(FillRect(canvas.dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))),
            "Cannot clear the DVD frame buffer.");
    RECT image{8, 8, static_cast<LONG>(width) - 8, static_cast<LONG>(height) - 8};
    const auto source = reinterpret_cast<WPARAM>(canvas.dc);
    require(SendMessageW(window, message, 0, dimensions) == 0 &&
                SendMessageW(window, message, source, MAKELONG(width - 1, height)) == 0 &&
                SendMessageW(window, message, reinterpret_cast<WPARAM>(window), dimensions) == 0,
            "DVD output accepted an invalid source or size.");

    for (const auto color : {RGB(255, 0, 255), RGB(0, 255, 0)}) {
        SetDCBrushColor(canvas.dc, color);
        require(FillRect(canvas.dc, &image, static_cast<HBRUSH>(GetStockObject(DC_BRUSH))),
                "Cannot compose the DVD frame.");
        require(SendMessageW(window, message, source, dimensions) != 0,
                "DVD output did not present the composed frame.");
        for (int sample = 0; sample < 4; ++sample) {
            pump();
            HDC dc = nullptr;
            require(SUCCEEDED(primary->GetDC(&dc)), "Cannot inspect the DVD primary surface.");
            const bool retained = GetPixel(dc, 0, 0) == RGB(0, 0, 0) &&
                                  GetPixel(dc, width / 2, height / 2) == color &&
                                  GetPixel(dc, width - 1, height - 1) == RGB(0, 0, 0);
            primary->ReleaseDC(dc);
            require(retained, "Concurrent display rendering replaced the DVD frame.");
        }
    }
}

void draw_frame(HWND window, IDirectDraw* draw, DWORD width, DWORD height, DWORD bits) {
    require(SUCCEEDED(draw->SetDisplayMode(width, height, bits)), "SetDisplayMode failed.");
    require(SendMessageW(window, RegisterWindowMessageW(L"XFilesEnhancement.DvdFrame"), 0, 0) == 0,
            "DVD output accepted a missing primary surface.");

    DDSURFACEDESC mode{};
    mode.dwSize = sizeof(mode);
    require(SUCCEEDED(draw->GetDisplayMode(&mode)), "GetDisplayMode failed.");
    require(mode.dwWidth == width && mode.dwHeight == height &&
                mode.ddpfPixelFormat.dwRGBBitCount == bits,
            "The logical display mode is wrong.");

    DDSURFACEDESC description{};
    description.dwSize = sizeof(description);
    description.dwFlags = DDSD_CAPS;
    description.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    ComPtr<IDirectDrawSurface> primary;
    require(SUCCEEDED(draw->CreateSurface(&description, &primary, nullptr)),
            "Create primary failed.");

    description.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    description.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    description.dwWidth = width;
    description.dwHeight = height;
    ComPtr<IDirectDrawSurface> frame;
    require(SUCCEEDED(draw->CreateSurface(&description, &frame, nullptr)),
            "Create offscreen failed.");

    const auto bind_draw = reinterpret_cast<BindDraw>(graphics_entry(Selector::QTSetDDObject));
    const auto bind_surface =
        reinterpret_cast<BindSurface>(graphics_entry(Selector::QTSetDDPrimarySurface));
    require(bind_draw(draw) == 0, "QTSetDDObject failed.");
    require(bind_surface(primary.Get(), 0) == 0, "QTSetDDPrimarySurface failed.");
    require(bind_draw(primary.Get()) == -50, "A surface was accepted as a DirectDraw object.");

    HDC dc = nullptr;
    require(SUCCEEDED(frame->GetDC(&dc)), "Surface GetDC failed.");
    RECT rectangle{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const auto brush = CreateSolidBrush(RGB(32, 96, 160));
    const bool filled = brush && FillRect(dc, &rectangle, brush);
    if (brush) {
        DeleteObject(brush);
    }
    const auto pixel = GetPixel(dc, 8, 8);
    const quickdraw::Rect update{10, 10, 30, 30};
    require(IntersectClipRect(dc, 10, 10, 20, 30) != ERROR, "Cannot clip the presentation source.");
    present_graphics(nullptr, dc, update);
    HDC presented = nullptr;
    require(SUCCEEDED(primary->GetDC(&presented)), "Cannot inspect the presented surface.");
    require(GetPixel(presented, 12, 12) == pixel && GetPixel(presented, 25, 12) != pixel &&
                GetPixel(presented, 8, 8) != pixel,
            "Partial presentation overwrote pixels outside its rectangle or clip.");
    primary->ReleaseDC(presented);
    SelectClipRgn(dc, nullptr);
    const auto released = frame->ReleaseDC(dc);
    require(filled && SUCCEEDED(released) && pixel != CLR_INVALID && pixel != 0,
            "GDI drawing into the frame failed.");
    require(SUCCEEDED(primary->Blt(nullptr, frame.Get(), nullptr, DDBLT_WAIT, nullptr)),
            "Blitting to the primary surface failed.");

    DDSURFACEDESC locked{};
    locked.dwSize = sizeof(locked);
    require(SUCCEEDED(primary->Lock(nullptr, &locked, DDLOCK_WAIT | DDLOCK_READONLY, nullptr)),
            "Locking the primary surface failed.");
    const auto* row = static_cast<const unsigned char*>(locked.lpSurface) + 8 * locked.lPitch;
    bool has_color = false;
    for (DWORD byte = 8 * bits / 8; byte < 9 * bits / 8; ++byte) {
        has_color |= row[byte] != 0;
    }
    require(SUCCEEDED(primary->Unlock(nullptr)), "Unlock failed.");
    require(has_color, "The primary surface did not receive the frame.");
    pump();
    dvd_frame(window, primary.Get(), width, height);
    release_graphics();
}

void exercise(HWND window, CreateDraw create_draw, const Desktop& before) {
    ComPtr<IDirectDraw> draw;
    require(SUCCEEDED(create_draw(nullptr, &draw, nullptr)), "DirectDrawCreate failed.");
    require(SUCCEEDED(draw->SetCooperativeLevel(window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN)),
            "SetCooperativeLevel failed.");

    for (const DWORD bits : {16UL, 32UL}) {
        draw_frame(window, draw.Get(), 640, 480, bits);
        check_desktop(before);
        draw_frame(window, draw.Get(), 800, 600, bits);
        check_desktop(before);
    }

    RECT requested_clip{0, 0, 640, 480};
    require(ClipCursor(&requested_clip), "The legacy cursor request failed.");
    check_desktop(before);
    require(ClipCursor(nullptr), "Releasing the legacy cursor request failed.");

    // Use the same toggle message as cnc-ddraw's Alt+Enter handler.
    constexpr UINT toggle_fullscreen = WM_APP + 117;
    SendMessageW(window, toggle_fullscreen, 2, 0);
    check_desktop(before);
    require(GetWindowLongW(window, GWL_STYLE) & WS_CAPTION, "Windowed mode has no border.");
    draw_frame(window, draw.Get(), 640, 480, 16);

    const auto display_message = RegisterWindowMessageW(L"XFilesEnhancement.DisplayMode");
    require(SendMessageW(window, display_message, 0, 0) == 1,
            "Display settings did not report windowed mode.");
    RECT work{};
    require(SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0), "Cannot read the work area.");
    RECT border{};
    require(AdjustWindowRectEx(&border, GetWindowLongW(window, GWL_STYLE), FALSE,
                               GetWindowLongW(window, GWL_EXSTYLE)),
            "Cannot measure the window border.");
    const auto available_width = work.right - work.left - (border.right - border.left);
    const auto available_height = work.bottom - work.top - (border.bottom - border.top);
    const auto size_message = RegisterWindowMessageW(L"XFilesEnhancement.WindowSize");
    // The release preset can exceed a virtual desktop; maximize then legitimately shrinks it.
    const auto initial_units = std::min({200L, available_width / 4, available_height / 3});
    const auto initial_width = initial_units * 4;
    const auto initial_height = initial_units * 3;
    require(initial_width >= 640 && initial_height >= 480,
            "Display test needs a work area that fits a 640x480 client plus window borders.");
    SendMessageW(window, size_message, initial_width, initial_height);
    pump();
    std::cout << "Work area: " << available_width << 'x' << available_height
              << "; initial client: " << initial_width << 'x' << initial_height << std::endl;
    WINDOWINFO restored{sizeof(WINDOWINFO)}, maximized{sizeof(WINDOWINFO)},
        again{sizeof(WINDOWINFO)};
    require(GetWindowInfo(window, &restored), "Cannot read restored window bounds.");
    SendMessageW(window, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    pump();
    require(GetWindowInfo(window, &maximized), "Cannot read maximized window bounds.");
    require(maximized.rcClient.right - maximized.rcClient.left >=
                    restored.rcClient.right - restored.rcClient.left &&
                maximized.rcClient.bottom - maximized.rcClient.top >=
                    restored.rcClient.bottom - restored.rcClient.top,
            "The first maximize shrank the window.");
    SendMessageW(window, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    pump();
    require(GetWindowInfo(window, &again) && EqualRect(&again.rcClient, &restored.rcClient),
            "Restore did not recover the previous window bounds.");
    require(SendMessageW(window, display_message, 2, 0) == 2 &&
                SendMessageW(window, display_message, 2, 0) == 2,
            "Borderless selection was not idempotent.");
    check_desktop(before);
    require(SendMessageW(window, display_message, 1, 0) == 1, "Windowed selection failed.");

    const auto wide_units = std::min({80L, available_width / 16, available_height / 9});
    const auto wide_width = wide_units * 16;
    const auto wide_height = wide_units * 9;
    require(wide_width >= 640 && wide_height >= 480,
            "Display test needs a work area that fits a 16:9 client at least 480 pixels high.");
    SendMessageW(window, size_message, wide_width, wide_height);
    pump();
    require(GetWindowInfo(window, &again) &&
                again.rcClient.right - again.rcClient.left == wide_width &&
                again.rcClient.bottom - again.rcClient.top == wide_height,
            "16:9 window preset did not set the physical client size.");
    const auto wide_size = SendMessageW(window, size_message, 0, 0);
    require(LOWORD(wide_size) == wide_width && HIWORD(wide_size) == wide_height,
            "16:9 window preset was not retained.");
    SendMessageW(window, display_message, 2, 0);
    SendMessageW(window, display_message, 1, 0);
    require(GetWindowInfo(window, &again) &&
                again.rcClient.right - again.rcClient.left == wide_width &&
                again.rcClient.bottom - again.rcClient.top == wide_height,
            "Borderless toggle lost the 16:9 window size.");
    require(SendMessageW(window, size_message, 12, 12) == wide_size,
            "Invalid display dimensions were accepted.");
    const auto filter_message = RegisterWindowMessageW(L"XFilesEnhancement.ScalingFilter");
    if (SendMessageW(window, filter_message, 0, 0)) {
        for (WPARAM filter = 1; filter <= 4; ++filter) {
            require(SendMessageW(window, filter_message, filter, 0) == static_cast<LRESULT>(filter),
                    "Scaling filter selection failed.");
            draw_frame(window, draw.Get(), 640, 480, 16);
            check_desktop(before);
        }
        SendMessageW(window, filter_message, 3, 0);
    }
    SendMessageW(window, size_message, 1280, 960);
    check_desktop(before);

    const auto game_cursor = LoadCursorW(nullptr, IDC_CROSS);
    SetCursor(game_cursor);
    SendMessageW(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window),
                 MAKELPARAM(HTBOTTOMRIGHT, WM_MOUSEMOVE));
    require(GetCursor() == LoadCursorW(nullptr, IDC_SIZENWSE), "Resize cursor was not selected.");
    SendMessageW(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window),
                 MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    require(GetCursor() == game_cursor, "The resize cursor remained over the client area.");
    SetCursor(nullptr);
    SendMessageW(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window),
                 MAKELPARAM(HTBOTTOMRIGHT, WM_MOUSEMOVE));
    SendMessageW(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window),
                 MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    require(GetCursor() == nullptr, "A hidden game cursor was not restored.");

    const auto tool_cursor = RegisterWindowMessageW(L"XFilesEnhancement.ToolCursor");
    const auto native_count = [] {
        const auto count = real_show_cursor(TRUE) - 1;
        real_show_cursor(FALSE);
        return count;
    };
    const auto original_count = native_count();
    int game_count = ShowCursor(FALSE);
    while (game_count >= -2) {
        game_count = ShowCursor(FALSE);
    }
    SetCursor(nullptr);
    SendMessageW(window, tool_cursor, 1, 0);
    require(native_count() >= 0 && GetCursor() == LoadCursorW(nullptr, IDC_ARROW),
            "Tool window did not reveal a hidden game cursor.");
    const auto visible_count = native_count();
    for (int i = 0; i < 10; ++i) {
        SendMessageW(window, tool_cursor, 1, 0);
    }
    require(native_count() == visible_count, "Tool cursor refresh changed the display count.");
    SetCursor(game_cursor);
    --game_count;
    ShowCursor(FALSE);
    require(GetCursor() == LoadCursorW(nullptr, IDC_ARROW) && native_count() >= 0,
            "Game cursor changes leaked into a tool window.");
    SendMessageW(window, tool_cursor, 3, 0);
    SendMessageW(window, tool_cursor, 0, 0);
    require(native_count() >= 0, "Deactivating inspector hid the nested settings cursor.");
    SendMessageW(window, tool_cursor, 4, 0);
    require(native_count() == game_count && GetCursor() == game_cursor,
            "Tool exit did not restore the game's latest cursor state.");
    while (game_count < original_count) {
        game_count = ShowCursor(TRUE);
    }
    SetCursor(nullptr);

    SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
    check_desktop(before);
    SendMessageW(window, WM_ACTIVATEAPP, TRUE, 0);
    check_desktop(before);

    SendMessageW(window, toggle_fullscreen, 1, 0);
    check_desktop(before);
    require(!(GetWindowLongW(window, GWL_STYLE) & WS_CAPTION), "Borderless mode has a border.");

    WINDOWINFO window_info{sizeof(window_info)};
    MONITORINFO monitor_info{sizeof(monitor_info)};
    require(GetWindowInfo(window, &window_info) &&
                GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor_info),
            "Cannot read the borderless window size.");
    const auto width = window_info.rcClient.right - window_info.rcClient.left;
    const auto height = window_info.rcClient.bottom - window_info.rcClient.top;
    const auto monitor_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
    const auto monitor_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
    require(width >= monitor_width && width <= monitor_width + 1 && height >= monitor_height &&
                height <= monitor_height + 1,
            "The borderless window does not cover the monitor.");
    std::cout << "Borderless client: " << width << 'x' << height << '\n';

    require(SUCCEEDED(draw->RestoreDisplayMode()), "RestoreDisplayMode failed.");
    check_desktop(before);
}

}

int wmain(int argc, wchar_t** argv) {
    HMODULE library = nullptr;
    HWND window = nullptr;
    int result = 1;
    const auto before = desktop_layout();
    // Keep the OS query before cnc-ddraw replaces the game's imported function.
    read_cursor_clip = reinterpret_cast<decltype(read_cursor_clip)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetClipCursor"));
    if (!read_cursor_clip) {
        return 1;
    }
    real_show_cursor = reinterpret_cast<decltype(real_show_cursor)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "ShowCursor"));
    if (!real_show_cursor) {
        return 1;
    }
    initial_clip = cursor_clip(read_cursor_clip);

    try {
        require(argc == 2, "Pass the built ddraw.dll path.");
        const auto path = std::filesystem::canonical(argv[1]);
        require(sha256(path) == ddraw_sha256, "Unexpected DirectDraw library.");
        require(sha256(path.parent_path() / L"ddraw.ini") == ddraw_config_sha256,
                "Unexpected DirectDraw settings.");
        require(SetEnvironmentVariableW(L"CNC_DDRAW_CONFIG_FILE",
                                        (path.parent_path() / L"ddraw.ini").c_str()),
                "Cannot select the test display settings.");
        library = LoadLibraryW(path.c_str());
        require(library != nullptr, "LoadLibrary failed.");
        const auto create_draw =
            reinterpret_cast<CreateDraw>(GetProcAddress(library, "DirectDrawCreate"));
        require(create_draw != nullptr, "DirectDrawCreate export missing.");

        WNDCLASSW window_class{};
        window_class.lpfnWndProc = DefWindowProcW;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"XFilesDisplayTest";
        require(RegisterClassW(&window_class) != 0, "RegisterClass failed.");
        window = CreateWindowExW(0, window_class.lpszClassName, L"X-Files display test",
                                 WS_OVERLAPPEDWINDOW, 100, 100, 640, 480, nullptr, nullptr,
                                 window_class.hInstance, nullptr);
        require(window != nullptr, "CreateWindow failed.");
        ShowWindow(window, SW_SHOWNOACTIVATE);
        exercise(window, create_draw, before);
        result = 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }

    release_graphics();
    if (window) {
        DestroyWindow(window);
    }
    if (library) {
        FreeLibrary(library);
    }
    if (cursor_clip(read_cursor_clip) != initial_clip) {
        ClipCursor(nullptr);
        std::cerr << "Cursor confinement remained after teardown; released it.\n";
        result = 1;
    }
    if (desktop_layout() != before) {
        std::cerr << "The monitor layout changed after teardown.\n";
        return 1;
    }
    if (result == 0) {
        write_desktop(std::cout, before);
        std::cout << "Drawing, mode switches, focus changes and teardown preserved the desktop and "
                     "cursor.\n";
    }
    return result;
}
