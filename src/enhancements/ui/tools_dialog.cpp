#include "tools_dialog.h"
#include "resources.h"
#include "enhancements/quick_save.h"
#include "enhancements/game_ui.h"
#include "diagnostics/report_dialog.h"
#include "identity.h"
#include "settings.h"

#include <commdlg.h>
#include <shellapi.h>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace enhancements {
namespace {
struct Tools {
    std::filesystem::path directory, checkpoint;
};

std::string summary(const std::filesystem::path& directory) {
    const auto identity = identify(directory / "XFiles.exe");
    const auto& value = settings();
    std::ostringstream text;
    text << "The X-Files enhancement " XFILES_BUILD_VERSION "\nEdition: " << identity.edition
         << "\nExecutable SHA256: " << identity.sha256 << "\nGamepad: " << value.gamepad
         << "\nAnalog pointer: " << value.analog_cursor
         << "\nSpring pointer: " << value.spring_cursor
         << "\nFocus highlight: " << static_cast<int>(value.focus_highlight)
         << "\nCaptions: " << static_cast<int>(value.captions)
         << "\nCaption font: " << static_cast<int>(value.caption_style.font)
         << "\nCaption size: " << value.caption_style.scale
         << "\nSkip login: " << value.skip_workstation_login
         << "\nSkip menu: " << value.skip_menu_animation << "\nInput class: 0x" << std::hex
         << game::input_vtable() << "\nResources:";
    const auto controls = game::script_controls();
    for (const auto id : controls.resources) {
        text << " 0x" << id;
    }
    text << std::dec << "\nButtons:\n";
    for (const auto& rect : controls.buttons) {
        text << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom << '\n';
    }
    text << "\nDescribe the problem and steps to reproduce when sharing this report.\n"
            "Logs may contain local file paths. A save is included only if selected; no game "
            "assets are included.\n";
    return text.str();
}

INT_PTR CALLBACK dialog_proc(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    auto* state = reinterpret_cast<Tools*>(GetWindowLongPtrW(window, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            SetWindowLongPtrW(window, DWLP_USER, data);
            SetDlgItemTextA(window, IDC_BUILD_VERSION, "Version " XFILES_BUILD_VERSION);
            EnableWindow(GetDlgItem(window, IDC_LOAD_CHECKPOINT), checkpoint_available());
            EnableWindow(GetDlgItem(window, IDC_EXPORT_SAVE), export_save_available());
            return TRUE;
        }
        if (message != WM_COMMAND || !state) {
            return FALSE;
        }
        switch (LOWORD(parameter)) {
            case IDC_ABOUT:
                MessageBoxA(window,
                            "The X-Files PC Enhancement Patch " XFILES_BUILD_VERSION "\n"
                            "Copyright (c) 2026 Zeffuro. MIT License.\n\n"
                            "Uses FFmpeg libraries under LGPL-2.1-or-later, cnc-ddraw (MIT), "
                            "and zlib (zlib license).\n\n"
                            "License notices are in the game folder. Matching FFmpeg source "
                            "and the build script are in the patch release ZIP.\n\n"
                            "Unofficial patch. Game content belongs to its owners.",
                            "About the patch", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            case IDC_LOAD_CHECKPOINT:
                if (checkpoint_available()) {
                    state->checkpoint =
                        diagnostics::choose_save_file(window, false, state->directory);
                    if (!state->checkpoint.empty()) {
                        EndDialog(window, IDOK);
                    }
                }
                return TRUE;
            case IDC_OPEN_LOGS:
                if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open",
                                                            state->directory.c_str(), nullptr,
                                                            nullptr, SW_SHOWNORMAL)) <= 32) {
                    throw std::runtime_error("Cannot open the logs folder");
                }
                return TRUE;
            case IDC_SAVE_REPORT:
                diagnostics::save_report_dialog(window, state->directory, summary(state->directory),
                                                IsDlgButtonChecked(window, IDC_REPORT_SAVE) ==
                                                    BST_CHECKED);
                return TRUE;
            case IDC_EXPORT_SAVE: {
                if (export_save_available()) {
                    const auto path = diagnostics::choose_save_file(window, true, state->directory);
                    if (!path.empty()) {
                        export_save(path);
                        MessageBoxW(window, L"Saved game exported.", L"The X-Files", MB_OK);
                    }
                }
                return TRUE;
            }
            case IDOK:
            case IDCANCEL:
                EndDialog(window, IDCANCEL);
                return TRUE;
        }
    } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "The X-Files tools", MB_OK | MB_ICONERROR);
    }
    return FALSE;
}
}

std::filesystem::path show_tools_dialog(HWND owner, HMODULE module) {
    std::wstring executable(32768, L'\0');
    const auto size =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!size || size >= executable.size()) {
        throw std::runtime_error("Cannot locate the game folder");
    }
    executable.resize(size);
    Tools state{std::filesystem::path(executable).parent_path(), {}};
    const auto result = DialogBoxParamW(module, MAKEINTRESOURCEW(IDD_TOOLS), owner, dialog_proc,
                                        reinterpret_cast<LPARAM>(&state));
    if (result == -1) {
        throw std::runtime_error("Cannot open game tools");
    }
    return result == IDOK ? state.checkpoint : std::filesystem::path{};
}
}
