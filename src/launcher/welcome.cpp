#include "welcome.h"
#include "localization/ui.h"
#include <windows.h>
#include <array>
#include <stdexcept>
#include <string>

namespace {
struct Choice {
    int control;
    const wchar_t* section;
    const wchar_t* key;
    bool recommended;
};

constexpr std::array choices{Choice{2101, L"Input", L"Gamepad", true},
                             Choice{2102, L"Input", L"AnalogCursor", true},
                             Choice{2103, L"Enhancements", L"MenuBlackBackground", true},
                             Choice{2104, L"Enhancements", L"SkipMenuAnimation", true},
                             Choice{2105, L"Enhancements", L"SaveBrowser", true},
                             Choice{2108, L"Input", L"Vibration", true},
                             Choice{2109, L"Input", L"ControllerHints", true},
                             Choice{2110, L"Video", L"DVDMovies", true},
                             Choice{2111, L"Video", L"DVDDeinterlace", true},
                             Choice{2112, L"Audio", L"MovieSpeedMute", true},
                             Choice{2116, L"Enhancements", L"DialogueTranscript", true},
                             Choice{2117, L"Enhancements", L"QuickMenu", true}};

}

bool save_welcome_choices(const std::filesystem::path& path, const WelcomeChoices& selected) {
    if (selected.movie_speed < 2 || selected.movie_speed > 4 || selected.captions > 2 ||
        selected.movie_colors > 3) {
        return false;
    }
    auto temporary = path;
    temporary += L".welcome-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(GetTickCount64());
    if (!CopyFileW(path.c_str(), temporary.c_str(), TRUE)) {
        return false;
    }
    bool written = true;
    for (std::size_t index = 0; index < choices.size(); ++index) {
        const auto& choice = choices[index];
        written =
            WritePrivateProfileStringW(choice.section, choice.key,
                                       selected.enabled[index] ? L"1" : L"0", temporary.c_str()) &&
            written;
    }
    const auto speed = std::to_wstring(selected.movie_speed);
    const auto captions = std::to_wstring(selected.captions);
    const auto colors = std::to_wstring(selected.movie_colors);
    written =
        WritePrivateProfileStringW(L"Video", L"MovieSpeed", speed.c_str(), temporary.c_str()) &&
        written;
    written = WritePrivateProfileStringW(L"Accessibility", L"Captions", captions.c_str(),
                                         temporary.c_str()) &&
              written;
    written =
        WritePrivateProfileStringW(L"Video", L"MovieContrast", colors.c_str(), temporary.c_str()) &&
        written;
    written =
        WritePrivateProfileStringW(L"Interface", L"WelcomeVersion", L"1", temporary.c_str()) &&
        written;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
    if (written && MoveFileExW(temporary.c_str(), path.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    DeleteFileW(temporary.c_str());
    return false;
}

namespace {
INT_PTR CALLBACK procedure(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    const auto* path =
        reinterpret_cast<const std::filesystem::path*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        path = reinterpret_cast<const std::filesystem::path*>(data);
        SetWindowLongPtrW(window, DWLP_USER, data);
        ui::translate_dialog(window);
        for (const auto& choice : choices) {
            CheckDlgButton(window, choice.control,
                           choice.recommended ? BST_CHECKED : BST_UNCHECKED);
        }
        for (const auto label : {L"2x", L"3x", L"4x"}) {
            SendDlgItemMessageW(window, 2113, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(ui::translate(label)));
        }
        for (const auto label : {L"Game preference", L"On", L"Off"}) {
            SendDlgItemMessageW(window, 2114, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(ui::translate(label)));
        }
        for (const auto label : {L"Original (off)", L"Reviewed scene grades",
                                 L"Contrast +15% (Cinepak)", L"Contrast +25% (Cinepak)"}) {
            SendDlgItemMessageW(window, 2115, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(ui::translate(label)));
        }
        for (const auto control : {2113, 2114, 2115}) {
            SendDlgItemMessageW(window, control, CB_SETCURSEL, 0, 0);
        }
        const auto gamepad = IsDlgButtonChecked(window, 2101) == BST_CHECKED;
        for (const auto control : {2102, 2108, 2109}) {
            EnableWindow(GetDlgItem(window, control), gamepad);
        }
        EnableWindow(GetDlgItem(window, 2111), IsDlgButtonChecked(window, 2110) == BST_CHECKED);
        return TRUE;
    }
    if (message == WM_CLOSE) {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (message != WM_COMMAND || !path) {
        return FALSE;
    }
    switch (LOWORD(parameter)) {
        case 2106:
        case 2107:
            for (const auto& choice : choices) {
                CheckDlgButton(window, choice.control,
                               LOWORD(parameter) == 2106 && choice.recommended ? BST_CHECKED
                                                                               : BST_UNCHECKED);
            }
            for (const auto control : {2113, 2114, 2115}) {
                SendDlgItemMessageW(window, control, CB_SETCURSEL, 0, 0);
            }
            [[fallthrough]];
        case 2101: {
            const auto gamepad = IsDlgButtonChecked(window, 2101) == BST_CHECKED;
            for (const auto control : {2102, 2108, 2109}) {
                EnableWindow(GetDlgItem(window, control), gamepad);
            }
            EnableWindow(GetDlgItem(window, 2111), IsDlgButtonChecked(window, 2110) == BST_CHECKED);
            return TRUE;
        }
        case 2110:
            EnableWindow(GetDlgItem(window, 2111), IsDlgButtonChecked(window, 2110) == BST_CHECKED);
            return TRUE;
        case IDOK: {
            WelcomeChoices selected;
            for (std::size_t index = 0; index < choices.size(); ++index) {
                selected.enabled[index] =
                    IsDlgButtonChecked(window, choices[index].control) == BST_CHECKED;
            }
            selected.movie_speed =
                2 + static_cast<unsigned>(SendDlgItemMessageW(window, 2113, CB_GETCURSEL, 0, 0));
            selected.captions =
                static_cast<unsigned>(SendDlgItemMessageW(window, 2114, CB_GETCURSEL, 0, 0));
            selected.movie_colors =
                static_cast<unsigned>(SendDlgItemMessageW(window, 2115, CB_GETCURSEL, 0, 0));
            if (save_welcome_choices(*path, selected)) {
                EndDialog(window, IDOK);
            } else {
                MessageBoxW(window, ui::translate(L"Cannot save your choices. Please try again."),
                            ui::translate(L"The X-Files enhancements"), MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(window, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
    }
}
}

bool show_welcome(const std::filesystem::path& directory) {
    const auto path = directory / L"patch.ini";
    if (GetPrivateProfileIntW(L"Interface", L"WelcomeVersion", 0, path.c_str()) >= 1) {
        return true;
    }
    const auto result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(115), nullptr,
                                        procedure, reinterpret_cast<LPARAM>(&path));
    if (result == -1) {
        throw std::runtime_error("Cannot open the first-launch options.");
    }
    return result == IDOK;
}
