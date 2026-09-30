#include "settings_dialog.h"
#include "tools_dialog.h"
#include "movie_preview.h"
#include "quick_menu_dialog.h"
#include "settings_tabs.h"
#include "controller_dialog.h"
#include "enhancements/quick_save.h"
#include "enhancements/edition.h"
#include "resources.h"
#include "settings.h"
#include "playback/output.h"
#include "playback/movie.h"
#include "devtools/inspector.h"
#include "platform/tool_cursor.h"
#include "platform/tool_theme.h"
#include "localization/ui.h"
#include "dvd/tools_pause.h"

#include <shellapi.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>

namespace enhancements {
namespace {

struct Dialog {
    std::vector<playback::OutputDevice> devices;
    std::filesystem::path checkpoint;
    Settings draft = settings();
    unsigned display_mode = 0;
    unsigned window_size = 0;
    unsigned scaling_filter = 0;
    unsigned original_filter = 0;
    bool inspect = false;
    SettingsTabs tabs;
};

struct WindowSize {
    const wchar_t* label;
    unsigned width;
    unsigned height;
};

bool dvd_movies_available() {
    if (game::edition().application != game::dvd.application) {
        return false;
    }
    std::vector<wchar_t> path(32768);
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) {
        return false;
    }
    const auto root = std::filesystem::path(path.data()).parent_path();
    std::error_code error;
    return std::filesystem::is_regular_file(root / L"vob" / L"teaser.vob", error) ||
           std::filesystem::is_regular_file(root / L"vob" / L"ddigital1.vob", error);
}

constexpr std::array window_sizes{
    WindowSize{L"Keep current size", 0, 0},
    WindowSize{L"640 x 480 (original 4:3)", 640, 480},
    WindowSize{L"800 x 600 (4:3)", 800, 600},
    WindowSize{L"960 x 720 (4:3)", 960, 720},
    WindowSize{L"1280 x 960 (4:3)", 1280, 960},
    WindowSize{L"1600 x 1200 (4:3)", 1600, 1200},
    WindowSize{L"1920 x 1440 (4:3)", 1920, 1440},
    WindowSize{L"1280 x 720 (16:9, side bars)", 1280, 720},
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
    if (message == WM_SETCURSOR) {
        SendMessageW(GetParent(window), RegisterWindowMessageW(L"XFilesEnhancement.ToolCursor"), 5,
                     0);
    }
    try {
        if (message == WM_INITDIALOG) {
            center_dialog(window);
            ui::translate_dialog(window);
            state = reinterpret_cast<Dialog*>(data);
            SetWindowLongPtrW(window, DWLP_USER, data);
            SetDlgItemTextA(window, IDC_BUILD_VERSION, "Version " XFILES_BUILD_VERSION);
            for (int index = 0; index < 6; ++index) {
                SendDlgItemMessageW(
                    window, IDC_INTERFACE_LANGUAGE, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(ui::language_name(static_cast<ui::Language>(index))));
            }
            SendDlgItemMessageW(window, IDC_INTERFACE_LANGUAGE, CB_SETCURSEL,
                                static_cast<WPARAM>(ui::language()), 0);
            state->display_mode =
                static_cast<unsigned>(SendMessageW(GetParent(window), display_message(), 0, 0));
            for (const auto label : {L"Windowed", L"Borderless fullscreen"}) {
                SendDlgItemMessageW(window, IDC_DISPLAY_MODE, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(ui::translate(label)));
            }
            SendDlgItemMessageW(window, IDC_DISPLAY_MODE, CB_SETCURSEL,
                                state->display_mode == 2 ? 1 : 0, 0);
            EnableWindow(GetDlgItem(window, IDC_DISPLAY_MODE), state->display_mode != 0);
            const auto size = SendMessageW(
                GetParent(window), RegisterWindowMessageW(L"XFilesEnhancement.WindowSize"), 0, 0);
            for (const auto& option : window_sizes) {
                SendDlgItemMessageW(window, IDC_WINDOW_SIZE, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(ui::translate(option.label)));
            }
            SendDlgItemMessageW(window, IDC_WINDOW_SIZE, CB_SETCURSEL, 0, 0);
            EnableWindow(GetDlgItem(window, IDC_WINDOW_SIZE), size != 0);
            state->scaling_filter = static_cast<unsigned>(
                SendMessageW(GetParent(window),
                             RegisterWindowMessageW(L"XFilesEnhancement.ScalingFilter"), 0, 0));
            state->original_filter = state->scaling_filter;
            for (const auto label : {L"Nearest neighbour", L"Bilinear (soft)", L"Bicubic (default)",
                                     L"Lanczos (sharp)"}) {
                SendDlgItemMessageW(window, IDC_SCALING_FILTER, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(ui::translate(label)));
            }
            SendDlgItemMessageW(window, IDC_SCALING_FILTER, CB_SETCURSEL,
                                state->scaling_filter ? state->scaling_filter - 1 : 2, 0);
            EnableWindow(GetDlgItem(window, IDC_SCALING_FILTER), state->scaling_filter != 0);
            for (const auto label : {L"Automatic", L"Always", L"Off"}) {
                SendDlgItemMessageW(window, IDC_FOCUS_HIGHLIGHT, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(ui::translate(label)));
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
                                    reinterpret_cast<LPARAM>(ui::translate(label)));
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
            CheckDlgButton(window, IDC_CAPTION_BACKGROUND,
                           settings().caption_style.background ? BST_CHECKED : BST_UNCHECKED);
            SendDlgItemMessageW(window, IDC_CAPTION_OPACITY, TBM_SETRANGE, TRUE,
                                MAKELPARAM(0, 100));
            SendDlgItemMessageW(window, IDC_CAPTION_OPACITY, TBM_SETPOS, TRUE,
                                settings().caption_style.opacity);
            for (const auto* name : {L"Black", L"Charcoal", L"Navy"}) {
                SendDlgItemMessageW(window, IDC_CAPTION_COLOR, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(ui::translate(name)));
            }
            const auto color = settings().caption_style.background_color;
            SendDlgItemMessageW(window, IDC_CAPTION_COLOR, CB_SETCURSEL,
                                color == RGB(40, 40, 40)   ? 1
                                : color == RGB(12, 24, 48) ? 2
                                                           : 0,
                                0);
            CheckDlgButton(window, IDC_GAMEPAD, settings().gamepad ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_VIBRATION,
                           settings().vibration ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_CONTROLLER_HINTS,
                           settings().controller_hints ? BST_CHECKED : BST_UNCHECKED);
            EnableWindow(GetDlgItem(window, IDC_VIBRATION), settings().gamepad);
            CheckDlgButton(window, IDC_ANALOG_CURSOR,
                           settings().analog_cursor ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_SPRING_CURSOR,
                           settings().spring_cursor ? BST_CHECKED : BST_UNCHECKED);
            EnableWindow(GetDlgItem(window, IDC_SPRING_CURSOR), settings().analog_cursor);
            CheckDlgButton(window, IDC_DVD_DEINTERLACE,
                           settings().dvd_deinterlace ? BST_CHECKED : BST_UNCHECKED);
            const bool dvd = dvd_movies_available();
            CheckDlgButton(window, IDC_DVD_MOVIES,
                           settings().dvd_movies ? BST_CHECKED : BST_UNCHECKED);
            EnableWindow(GetDlgItem(window, IDC_DVD_MOVIES), dvd);
            EnableWindow(GetDlgItem(window, IDC_DVD_DEINTERLACE), dvd && settings().dvd_movies);
            for (const auto label : {L"Original (off)", L"Reviewed scene grades",
                                     L"Contrast +15% (Cinepak)", L"Contrast +25% (Cinepak)"}) {
                SendDlgItemMessageW(window, IDC_MOVIE_CONTRAST, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(ui::translate(label)));
            }
            SendDlgItemMessageW(window, IDC_MOVIE_CONTRAST, CB_SETCURSEL,
                                static_cast<WPARAM>(settings().movie_contrast), 0);
            for (const auto label : {L"2x", L"3x", L"4x"}) {
                SendDlgItemMessageW(window, IDC_MOVIE_SPEED, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(label));
            }
            SendDlgItemMessageW(window, IDC_MOVIE_SPEED, CB_SETCURSEL, settings().movie_speed - 2,
                                0);
            CheckDlgButton(window, IDC_MOVIE_SPEED_MUTE,
                           settings().movie_speed_mute ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_MENU_BLACK,
                           settings().menu_black_background ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_SKIP_LOGIN,
                           settings().skip_workstation_login ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_SKIP_MENU,
                           settings().skip_menu_animation ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_SAVE_BROWSER,
                           settings().save_browser ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(window, IDC_DIALOGUE_TRANSCRIPT,
                           settings().dialogue_transcript ? BST_CHECKED : BST_UNCHECKED);
            state->tabs.initialize(window);
            return TRUE;
        }
        if (message == WM_NOTIFY && state) {
            const auto* notification = reinterpret_cast<NMHDR*>(data);
            if (notification->idFrom == IDC_SETTINGS_TABS && notification->code == TCN_SELCHANGE) {
                state->tabs.select(
                    static_cast<unsigned>(TabCtrl_GetCurSel(notification->hwndFrom)));
                return TRUE;
            }
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
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_DVD_MOVIES) {
            EnableWindow(GetDlgItem(window, IDC_DVD_DEINTERLACE),
                         dvd_movies_available() &&
                             IsDlgButtonChecked(window, IDC_DVD_MOVIES) == BST_CHECKED);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_SCALING_FILTER &&
            HIWORD(parameter) == CBN_SELCHANGE && state && state->scaling_filter) {
            const auto filter = SendDlgItemMessageW(window, IDC_SCALING_FILTER, CB_GETCURSEL, 0, 0);
            if (filter >= 0 && filter <= 3) {
                SendMessageW(GetParent(window),
                             RegisterWindowMessageW(L"XFilesEnhancement.ScalingFilter"), filter + 1,
                             0);
            }
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_MOVIE_PREVIEW) {
            const auto selected =
                SendDlgItemMessageW(window, IDC_MOVIE_CONTRAST, CB_GETCURSEL, 0, 0);
            const auto mode = show_movie_preview(
                window, reinterpret_cast<HMODULE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)),
                static_cast<MovieContrast>(selected));
            SendDlgItemMessageW(window, IDC_MOVIE_CONTRAST, CB_SETCURSEL, static_cast<WPARAM>(mode),
                                0);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_ANALOG_CURSOR) {
            EnableWindow(GetDlgItem(window, IDC_SPRING_CURSOR),
                         IsDlgButtonChecked(window, IDC_ANALOG_CURSOR) == BST_CHECKED);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_GAMEPAD) {
            EnableWindow(GetDlgItem(window, IDC_VIBRATION),
                         IsDlgButtonChecked(window, IDC_GAMEPAD) == BST_CHECKED);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_QUICK_MENU && state) {
            show_quick_menu_dialog(
                window, reinterpret_cast<HMODULE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)),
                state->draft);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_CONFIGURE_CONTROLLER && state) {
            show_controller_dialog(
                window, reinterpret_cast<HMODULE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)),
                state->draft);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDC_TOOLS && state) {
            const auto tools = show_tools_dialog(
                window, reinterpret_cast<HMODULE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)));
            state->checkpoint = tools.checkpoint;
            if (tools.inspect) {
                state->inspect = true;
                SendMessageW(window, WM_COMMAND, IDOK, 0);
                state->inspect = false;
            }
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
            value.vibration = IsDlgButtonChecked(window, IDC_VIBRATION) == BST_CHECKED;
            value.controller_hints =
                IsDlgButtonChecked(window, IDC_CONTROLLER_HINTS) == BST_CHECKED;
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
                state->tabs.select(2);
                SetFocus(GetDlgItem(window, font < 0 || static_cast<std::size_t>(font) >=
                                                            caption_fonts.size()
                                                ? IDC_CAPTION_FONT
                                                : IDC_CAPTION_SCALE));
                throw std::runtime_error(
                    "Select a caption font and a size between 75 and 200 percent");
            }
            value.caption_style = {static_cast<CaptionFont>(font), scale,
                                   IsDlgButtonChecked(window, IDC_CAPTION_BACKGROUND) ==
                                       BST_CHECKED,
                                   CaptionPlacement::Inside};
            value.caption_style.opacity = static_cast<unsigned>(
                SendDlgItemMessageW(window, IDC_CAPTION_OPACITY, TBM_GETPOS, 0, 0));
            const auto color = SendDlgItemMessageW(window, IDC_CAPTION_COLOR, CB_GETCURSEL, 0, 0);
            value.caption_style.background_color = color == 1   ? RGB(40, 40, 40)
                                                   : color == 2 ? RGB(12, 24, 48)
                                                                : RGB(0, 0, 0);
            value.dvd_deinterlace = IsDlgButtonChecked(window, IDC_DVD_DEINTERLACE) == BST_CHECKED;
            value.dvd_movies = IsDlgButtonChecked(window, IDC_DVD_MOVIES) == BST_CHECKED;
            const auto speed = SendDlgItemMessageW(window, IDC_MOVIE_SPEED, CB_GETCURSEL, 0, 0);
            if (speed >= 0 && speed <= 2) {
                value.movie_speed = static_cast<unsigned>(speed) + 2;
            }
            value.movie_speed_mute =
                IsDlgButtonChecked(window, IDC_MOVIE_SPEED_MUTE) == BST_CHECKED;
            const auto contrast =
                SendDlgItemMessageW(window, IDC_MOVIE_CONTRAST, CB_GETCURSEL, 0, 0);
            if (contrast >= 0 && contrast <= static_cast<LRESULT>(MovieContrast::Medium)) {
                value.movie_contrast = static_cast<MovieContrast>(contrast);
            }
            value.menu_black_background = IsDlgButtonChecked(window, IDC_MENU_BLACK) == BST_CHECKED;
            value.skip_workstation_login =
                IsDlgButtonChecked(window, IDC_SKIP_LOGIN) == BST_CHECKED;
            value.skip_menu_animation = IsDlgButtonChecked(window, IDC_SKIP_MENU) == BST_CHECKED;
            value.save_browser = IsDlgButtonChecked(window, IDC_SAVE_BROWSER) == BST_CHECKED;
            value.dialogue_transcript =
                IsDlgButtonChecked(window, IDC_DIALOGUE_TRANSCRIPT) == BST_CHECKED;
            value.quick_menu = state->draft.quick_menu;
            value.quick_menu_items = state->draft.quick_menu_items;
            value.controller_profile = state->draft.controller_profile;
            const auto selected_language =
                SendDlgItemMessageW(window, IDC_INTERFACE_LANGUAGE, CB_GETCURSEL, 0, 0);
            if (selected_language < 0 || selected_language >= 6) {
                throw std::runtime_error("Select an interface language");
            }
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
                if (size < 0 || static_cast<std::size_t>(size) >= window_sizes.size()) {
                    throw std::runtime_error("Select a window size");
                }
                state->window_size = static_cast<unsigned>(size);
                if (size &&
                    (!WritePrivateProfileStringW(L"ddraw", L"width",
                                                 std::to_wstring(window_sizes[size].width).c_str(),
                                                 path.c_str()) ||
                     !WritePrivateProfileStringW(L"ddraw", L"height",
                                                 std::to_wstring(window_sizes[size].height).c_str(),
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
            std::wstring module_path(32768, L'\0');
            const auto module_length = GetModuleFileNameW(nullptr, module_path.data(),
                                                          static_cast<DWORD>(module_path.size()));
            if (!module_length || module_length >= module_path.size()) {
                throw std::runtime_error("Cannot locate enhancement settings");
            }
            module_path.resize(module_length);
            ui::save_language(std::filesystem::path(module_path).parent_path() / L"patch.ini",
                              static_cast<ui::Language>(selected_language));
            if (state->inspect) {
                devtools::request_inspector();
            }
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
    dvd::ToolPause dvd_pause;
    platform::ToolCursor cursor(window);

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
        platform::ToolTheme theme(module);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_TAB_CLASSES};
        if (!InitCommonControlsEx(&controls)) {
            throw std::runtime_error("Cannot initialize caption settings");
        }
        const auto result = DialogBoxParamW(module, MAKEINTRESOURCEW(IDD_ENHANCEMENTS), window,
                                            dialog_proc, reinterpret_cast<LPARAM>(&state));
        if (result == -1) {
            throw std::runtime_error("Cannot open enhancement settings");
        }
        if (result == IDC_LOAD_CHECKPOINT) {
            checkpoint = state.checkpoint;
        }
        if (result != IDOK && state.original_filter) {
            SendMessageW(window, RegisterWindowMessageW(L"XFilesEnhancement.ScalingFilter"),
                         state.original_filter, 0);
        }
        if (result == IDOK && state.display_mode) {
            if (state.window_size) {
                const auto& size = window_sizes[state.window_size];
                SendMessageW(window, RegisterWindowMessageW(L"XFilesEnhancement.WindowSize"),
                             size.width, size.height);
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
