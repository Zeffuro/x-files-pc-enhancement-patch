#include "settings_dialog.h"
#include "tools_dialog.h"
#include "enhancements/quick_save.h"
#include "resources.h"
#include "settings.h"
#include "playback/output.h"
#include "playback/movie.h"

#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace enhancements {
namespace {

struct Dialog {
    std::vector<playback::OutputDevice> devices;
    std::filesystem::path checkpoint;
    unsigned display_mode = 0;
    unsigned window_size = 0;
    unsigned scaling_filter = 0;
};

UINT display_message() {
    static const UINT message = RegisterWindowMessageW(L"XFilesEnhancement.DisplayMode");
    return message;
}

void center_dialog(HWND window) {
    // These APIs retain desktop coordinates under cnc-ddraw's window hooks.
    WINDOWINFO owner_info{sizeof(WINDOWINFO)}, dialog_info{sizeof(WINDOWINFO)};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetWindowInfo(GetParent(window), &owner_info) || !GetWindowInfo(window, &dialog_info) ||
        !GetMonitorInfoW(MonitorFromWindow(GetParent(window), MONITOR_DEFAULTTONEAREST),
                         &monitor)) {
        return;
    }
    const auto& owner = owner_info.rcWindow;
    const auto& dialog = dialog_info.rcWindow;
    const auto width = dialog.right - dialog.left;
    const auto height = dialog.bottom - dialog.top;
    const auto& area = monitor.rcWork;
    const auto x = std::clamp(owner.left + (owner.right - owner.left - width) / 2, area.left,
                              std::max(area.left, area.right - width));
    const auto y = std::clamp(owner.top + (owner.bottom - owner.top - height) / 2, area.top,
                              std::max(area.top, area.bottom - height));
    auto positions = BeginDeferWindowPos(1);
    if (positions) {
        positions = DeferWindowPos(positions, window, nullptr, x, y, 0, 0,
                                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (positions) {
            EndDeferWindowPos(positions);
        }
    }
}

INT_PTR CALLBACK dialog_proc(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    auto* state = reinterpret_cast<Dialog*>(GetWindowLongPtrW(window, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            center_dialog(window);
            state = reinterpret_cast<Dialog*>(data);
            SetWindowLongPtrW(window, DWLP_USER, data);
            SetDlgItemTextA(window, IDC_BUILD_VERSION, "Version " XFILES_BUILD_VERSION);
            state->display_mode =
                static_cast<unsigned>(SendMessageW(GetParent(window), display_message(), 0, 0));
            for (const auto label : {L"Windowed", L"Borderless fullscreen"}) {
                SendDlgItemMessageW(window, IDC_DISPLAY_MODE, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(label));
            }
            SendDlgItemMessageW(window, IDC_DISPLAY_MODE, CB_SETCURSEL,
                                state->display_mode == 2 ? 1 : 0, 0);
            EnableWindow(GetDlgItem(window, IDC_DISPLAY_MODE), state->display_mode != 0);
            const auto size = SendMessageW(
                GetParent(window), RegisterWindowMessageW(L"XFilesEnhancement.WindowSize"), 0, 0);
            for (const auto label :
                 {L"Keep current size", L"1280 x 960 (4:3)", L"1280 x 720 (16:9, side bars)"}) {
                SendDlgItemMessageW(window, IDC_WINDOW_SIZE, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(label));
            }
            SendDlgItemMessageW(window, IDC_WINDOW_SIZE, CB_SETCURSEL, 0, 0);
            EnableWindow(GetDlgItem(window, IDC_WINDOW_SIZE), size != 0);
            state->scaling_filter = static_cast<unsigned>(
                SendMessageW(GetParent(window),
                             RegisterWindowMessageW(L"XFilesEnhancement.ScalingFilter"), 0, 0));
            for (const auto label : {L"Nearest neighbour", L"Bilinear (soft)", L"Bicubic (default)",
                                     L"Lanczos (sharp)"}) {
                SendDlgItemMessageW(window, IDC_SCALING_FILTER, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(label));
            }
            SendDlgItemMessageW(window, IDC_SCALING_FILTER, CB_SETCURSEL,
                                state->scaling_filter ? state->scaling_filter - 1 : 2, 0);
            EnableWindow(GetDlgItem(window, IDC_SCALING_FILTER), state->scaling_filter != 0);
            for (const auto label : {L"Automatic", L"Always", L"Off"}) {
                SendDlgItemMessageW(window, IDC_FOCUS_HIGHLIGHT, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(label));
            }
            SendDlgItemMessageW(window, IDC_FOCUS_HIGHLIGHT, CB_SETCURSEL,
                                static_cast<WPARAM>(settings().focus_highlight), 0);
            const auto combo = GetDlgItem(window, IDC_AUDIO_DEVICE);
            int selected = 0;
            for (std::size_t index = 0; index < state->devices.size(); ++index) {
                const auto& device = state->devices[index];
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(device.name.c_str()));
                if (device.id == settings().audio_device) {
                    selected = static_cast<int>(index);
                }
            }
            SendMessageW(combo, CB_SETCURSEL, selected, 0);
            for (const auto label : {L"Game preference", L"On", L"Off"}) {
                SendDlgItemMessageW(window, IDC_CAPTIONS, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(label));
            }
            SendDlgItemMessageW(window, IDC_CAPTIONS, CB_SETCURSEL,
                                static_cast<WPARAM>(settings().captions), 0);
            for (const auto& font : caption_fonts) {
                SendDlgItemMessageW(window, IDC_CAPTION_FONT, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(font.name));
            }
            SendDlgItemMessageW(window, IDC_CAPTION_FONT, CB_SETCURSEL,
                                static_cast<WPARAM>(settings().caption_style.font), 0);
            SetDlgItemInt(window, IDC_CAPTION_SCALE, settings().caption_style.scale, FALSE);
            CheckDlgButton(window, IDC_GAMEPAD, settings().gamepad ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_ANALOG_CURSOR,
                           settings().analog_cursor ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_SPRING_CURSOR,
                           settings().spring_cursor ? BST_CHECKED : BST_UNCHECKED);
            EnableWindow(GetDlgItem(window, IDC_SPRING_CURSOR), settings().analog_cursor);
            CheckDlgButton(window, IDC_MENU_BLACK,
                           settings().menu_black_background ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_SKIP_LOGIN,
                           settings().skip_workstation_login ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_SKIP_MENU,
                           settings().skip_menu_animation ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_GITHUB &&
            HIWORD(parameter) == BN_CLICKED) {
            const auto result = ShellExecuteW(
                window, L"open", L"https://github.com/Zeffuro/x-files-pc-enhancement-patch",
                nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32) {
                throw std::runtime_error("Cannot open the GitHub page");
            }
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_ANALOG_CURSOR) {
            EnableWindow(GetDlgItem(window, IDC_SPRING_CURSOR),
                         IsDlgButtonChecked(window, IDC_ANALOG_CURSOR) == BST_CHECKED);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_TOOLS && state) {
            state->checkpoint = show_tools_dialog(
                window, reinterpret_cast<HMODULE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)));
            if (!state->checkpoint.empty()) {
                EndDialog(window, IDC_LOAD_CHECKPOINT);
            }
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDOK && state) {
            const auto index = SendDlgItemMessageW(window, IDC_AUDIO_DEVICE, CB_GETCURSEL, 0, 0);
            if (index < 0 || static_cast<std::size_t>(index) >= state->devices.size()) {
                throw std::runtime_error("Select an audio output device");
            }
            auto value = settings();
            const auto highlight =
                SendDlgItemMessageW(window, IDC_FOCUS_HIGHLIGHT, CB_GETCURSEL, 0, 0);
            if (highlight < 0 || highlight > static_cast<int>(FocusHighlight::Off)) {
                throw std::runtime_error("Select a controller highlight mode");
            }
            value.focus_highlight = static_cast<FocusHighlight>(highlight);
            value.audio_device = state->devices[index].id;
            value.gamepad = IsDlgButtonChecked(window, IDC_GAMEPAD) == BST_CHECKED;
            value.analog_cursor = IsDlgButtonChecked(window, IDC_ANALOG_CURSOR) == BST_CHECKED;
            value.spring_cursor = IsDlgButtonChecked(window, IDC_SPRING_CURSOR) == BST_CHECKED;
            const auto captions = SendDlgItemMessageW(window, IDC_CAPTIONS, CB_GETCURSEL, 0, 0);
            if (captions < 0 || captions > static_cast<int>(CaptionMode::Off)) {
                throw std::runtime_error("Select a caption setting");
            }
            value.captions = static_cast<CaptionMode>(captions);
            const auto font = SendDlgItemMessageW(window, IDC_CAPTION_FONT, CB_GETCURSEL, 0, 0);
            BOOL valid = FALSE;
            const auto scale = GetDlgItemInt(window, IDC_CAPTION_SCALE, &valid, FALSE);
            if (font < 0 || static_cast<std::size_t>(font) >= caption_fonts.size() || !valid ||
                scale < 75 || scale > 200) {
                throw std::runtime_error(
                    "Select a caption font and a size between 75 and 200 percent");
            }
            value.caption_style = {static_cast<CaptionFont>(font), scale};
            value.menu_black_background = IsDlgButtonChecked(window, IDC_MENU_BLACK) == BST_CHECKED;
            value.skip_workstation_login =
                IsDlgButtonChecked(window, IDC_SKIP_LOGIN) == BST_CHECKED;
            value.skip_menu_animation = IsDlgButtonChecked(window, IDC_SKIP_MENU) == BST_CHECKED;
            if (state->display_mode) {
                const auto mode = SendDlgItemMessageW(window, IDC_DISPLAY_MODE, CB_GETCURSEL, 0, 0);
                if (mode != 0 && mode != 1) {
                    throw std::runtime_error("Select a display mode");
                }
                std::wstring executable(32768, L'\0');
                const auto length = GetModuleFileNameW(nullptr, executable.data(),
                                                       static_cast<DWORD>(executable.size()));
                if (!length || length >= executable.size()) {
                    throw std::runtime_error("Cannot locate the game executable");
                }
                executable.resize(length);
                if (!WritePrivateProfileStringW(
                        L"ddraw", L"fullscreen", mode == 1 ? L"true" : L"false",
                        (std::filesystem::path(executable).parent_path() / L"ddraw.ini").c_str())) {
                    throw std::runtime_error("Cannot save display settings");
                }
                state->display_mode = static_cast<unsigned>(mode + 1);
                const auto path = std::filesystem::path(executable).parent_path() / L"ddraw.ini";
                const auto size = SendDlgItemMessageW(window, IDC_WINDOW_SIZE, CB_GETCURSEL, 0, 0);
                if (size < 0 || size > 2) {
                    throw std::runtime_error("Select a window size");
                }
                state->window_size = static_cast<unsigned>(size);
                if (size &&
                    (!WritePrivateProfileStringW(L"ddraw", L"width", L"1280", path.c_str()) ||
                     !WritePrivateProfileStringW(L"ddraw", L"height", size == 1 ? L"960" : L"720",
                                                 path.c_str()))) {
                    throw std::runtime_error("Cannot save window size");
                }
                if (state->scaling_filter) {
                    const auto filter =
                        SendDlgItemMessageW(window, IDC_SCALING_FILTER, CB_GETCURSEL, 0, 0);
                    if (filter < 0 || filter > 3 ||
                        !WritePrivateProfileStringW(L"ddraw", L"d3d9_filter",
                                                    std::to_wstring(filter).c_str(),
                                                    path.c_str())) {
                        throw std::runtime_error("Cannot save scaling filter");
                    }
                    state->scaling_filter = static_cast<unsigned>(filter + 1);
                }
            }
            save_settings(value);
            EndDialog(window, IDOK);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDCANCEL) {
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
    } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "The X-Files enhancements", MB_OK | MB_ICONERROR);
        if (message == WM_INITDIALOG) {
            EndDialog(window, IDCANCEL);
        }
    }
    return FALSE;
}

}

void show_settings_dialog(HWND window) {
    struct DialogCursor {
        HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        unsigned increments = 1;

        DialogCursor() {
            while (ShowCursor(TRUE) < 0) {
                ++increments;
            }
        }

        ~DialogCursor() {
            while (increments) {
                --increments;
                ShowCursor(FALSE);
            }
            SetCursor(previous);
        }
    } cursor;

    std::vector<playback::MovieHandle> paused;
    std::filesystem::path checkpoint;
    try {
        Dialog state{playback::output_devices(), {}};
        paused = playback::pause_movies();
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&dialog_proc), &module) ||
            !module) {
            throw std::runtime_error("Cannot open enhancement settings");
        }
        const auto result = DialogBoxParamW(module, MAKEINTRESOURCEW(IDD_ENHANCEMENTS), window,
                                            dialog_proc, reinterpret_cast<LPARAM>(&state));
        if (result == -1) {
            throw std::runtime_error("Cannot open enhancement settings");
        }
        if (result == IDC_LOAD_CHECKPOINT) {
            checkpoint = state.checkpoint;
        }
        if (result == IDOK && state.display_mode) {
            if (state.window_size) {
                SendMessageW(window, RegisterWindowMessageW(L"XFilesEnhancement.WindowSize"), 1280,
                             state.window_size == 1 ? 960 : 720);
            }
            SendMessageW(window, display_message(), state.display_mode, 0);
            if (state.scaling_filter) {
                SendMessageW(window, RegisterWindowMessageW(L"XFilesEnhancement.ScalingFilter"),
                             state.scaling_filter, 0);
            }
        }
    } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "The X-Files enhancements", MB_OK | MB_ICONERROR);
    }
    playback::resume_movies(paused);
    if (!checkpoint.empty()) {
        try {
            load_checkpoint(window, checkpoint);
        } catch (const std::exception& error) {
            MessageBoxA(window, error.what(), "The X-Files checkpoint", MB_OK | MB_ICONERROR);
        }
    }
}

}
