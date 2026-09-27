#include "welcome.h"
#include "localization/ui.h"
#include <windows.h>
#include <array>
#include <stdexcept>

namespace {
struct Choice {
    int control;
    const wchar_t* section;
    const wchar_t* key;
    bool initial;
    bool recommended;
};

constexpr std::array choices{Choice{2101, L"Input", L"Gamepad", true, true},
                             Choice{2102, L"Input", L"AnalogCursor", true, true},
                             Choice{2103, L"Enhancements", L"MenuBlackBackground", true, true},
                             Choice{2104, L"Enhancements", L"SkipMenuAnimation", false, true},
                             Choice{2105, L"Enhancements", L"SaveBrowser", false, true}};

}

bool save_welcome_choices(const std::filesystem::path& path, const std::array<bool, 5>& enabled) {
    auto temporary = path;
    temporary += L".welcome-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(GetTickCount64());
    if (!CopyFileW(path.c_str(), temporary.c_str(), TRUE)) {
        return false;
    }
    bool written = true;
    for (std::size_t index = 0; index < choices.size(); ++index) {
        const auto& choice = choices[index];
        written = WritePrivateProfileStringW(choice.section, choice.key,
                                             enabled[index] ? L"1" : L"0", temporary.c_str()) &&
                  written;
    }
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
            CheckDlgButton(
                window, choice.control,
                GetPrivateProfileIntW(choice.section, choice.key, choice.initial, path->c_str())
                    ? BST_CHECKED
                    : BST_UNCHECKED);
        }
        EnableWindow(GetDlgItem(window, 2102), IsDlgButtonChecked(window, 2101) == BST_CHECKED);
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
            [[fallthrough]];
        case 2101:
            EnableWindow(GetDlgItem(window, 2102), IsDlgButtonChecked(window, 2101) == BST_CHECKED);
            return TRUE;
        case IDOK: {
            std::array<bool, choices.size()> enabled{};
            for (std::size_t index = 0; index < choices.size(); ++index) {
                enabled[index] = IsDlgButtonChecked(window, choices[index].control) == BST_CHECKED;
            }
            if (save_welcome_choices(*path, enabled)) {
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
