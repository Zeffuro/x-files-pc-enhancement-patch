#include "shortcuts.h"
#include "dialog.h"
#include "media.h"

#include <windows.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr int source_field = 1101, source_browse = 1102, source_iso = 1110;
constexpr int destination_field = 1103, destination_browse = 1104, windowed_field = 1105;
constexpr int status_field = 1106, progress_field = 1107;
constexpr int desktop_field = 1108, start_menu_field = 1109;
constexpr UINT progress_message = WM_APP + 1, completed_message = WM_APP + 2;

struct Setup {
    std::thread worker;
    std::atomic<bool> cancel{false};
    std::filesystem::path destination;
    std::string error;
    bool busy = false, ready = false;
};

std::filesystem::path field(HWND window, int id) {
    std::wstring text(32768, L'\0');
    text.resize(GetDlgItemTextW(window, id, text.data(), static_cast<int>(text.size())));
    if (text.empty()) {
        throw std::runtime_error("Choose your game files and an installation folder.");
    }
    return text;
}

std::filesystem::path browse(HWND owner, const wchar_t* title, bool iso = false) {
    IFileOpenDialog* dialog = nullptr;
    const auto created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&dialog));
    if (FAILED(created)) {
        throw std::runtime_error("Cannot open the file picker.");
    }

    struct Release {
        IFileOpenDialog* dialog;

        ~Release() {
            dialog->Release();
        }
    } release{dialog};

    DWORD options = FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    options |= iso ? FOS_FILEMUSTEXIST : FOS_PICKFOLDERS;
    if (FAILED(dialog->SetOptions(options)) || FAILED(dialog->SetTitle(title))) {
        throw std::runtime_error("Cannot configure the file picker.");
    }
    if (iso) {
        const COMDLG_FILTERSPEC types[] = {{L"DVD images (*.iso)", L"*.iso"}};
        if (FAILED(dialog->SetFileTypes(1, types))) {
            throw std::runtime_error("Cannot filter DVD images.");
        }
    }
    const auto shown = dialog->Show(owner);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return {};
    }
    IShellItem* item = nullptr;
    if (FAILED(shown) || FAILED(dialog->GetResult(&item))) {
        throw std::runtime_error("Cannot read the selected path.");
    }
    PWSTR name = nullptr;
    const auto named = item->GetDisplayName(SIGDN_FILESYSPATH, &name);
    item->Release();
    if (FAILED(named)) {
        throw std::runtime_error("Select a local folder, mounted disc or DVD ISO.");
    }
    const std::filesystem::path result(name);
    CoTaskMemFree(name);
    return result;
}

std::filesystem::path default_destination() {
    std::wstring windows(32768, L'\0');
    const auto length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (!length || length >= windows.size()) {
        return L"C:\\Games\\The X-Files";
    }
    windows.resize(length);
    return std::filesystem::path(windows).root_path() / L"Games" / L"The X-Files";
}

void enable_fields(HWND window, bool enabled) {
    for (int id : {source_field, source_browse, source_iso, destination_field, destination_browse,
                   windowed_field, desktop_field, start_menu_field, IDOK}) {
        EnableWindow(GetDlgItem(window, id), enabled);
    }
}

void start(HWND window, Setup& setup) {
    const auto source = field(window, source_field);
    const auto destination =
        std::filesystem::absolute(field(window, destination_field)).lexically_normal();
    const bool windowed = IsDlgButtonChecked(window, windowed_field) == BST_CHECKED;
    setup.destination = destination;
    setup.error.clear();
    setup.cancel = false;
    setup.busy = true;
    enable_fields(window, false);
    SetDlgItemTextW(window, status_field, L"Checking game files and free space...");
    SendDlgItemMessageW(window, progress_field, PBM_SETPOS, 0, 0);
    setup.worker = std::thread([window, &setup, source, destination, windowed] {
        try {
            const auto media = inspect_media(source);
            if (setup.cancel) {
                throw std::runtime_error("Setup cancelled. No files were changed.");
            }
            install_media(media, destination, windowed, [&](unsigned percent) {
                PostMessageW(window, progress_message, percent, 0);
                return !setup.cancel;
            });
        } catch (const std::exception& error) {
            setup.error = error.what();
        }
        PostMessageW(window, completed_message, 0, 0);
    });
}

void play(const std::filesystem::path& directory) {
    const auto executable = directory / L"XFilesPlay.exe";
    std::wstring command = L"\"" + executable.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        directory.c_str(), &startup, &process)) {
        throw std::runtime_error(
            "Cannot start the game. Open XFilesPlay.exe from the installed folder.");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

INT_PTR CALLBACK procedure(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    auto setup = reinterpret_cast<Setup*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(window, DWLP_USER, data);
        SendDlgItemMessageW(window, progress_field, PBM_SETRANGE32, 0, 100);
        try {
            SetDlgItemTextW(window, destination_field, default_destination().c_str());
        } catch (const std::exception& error) {
            MessageBoxA(window, error.what(), "The X-Files Setup", MB_OK | MB_ICONWARNING);
        }
        return TRUE;
    }
    if (!setup) {
        return FALSE;
    }
    if (message == progress_message) {
        SetDlgItemTextW(window, status_field, L"Copying and verifying game files...");
        SendDlgItemMessageW(window, progress_field, PBM_SETPOS, parameter, 0);
        return TRUE;
    }
    if (message == completed_message) {
        setup->worker.join();
        setup->busy = false;
        EnableWindow(GetDlgItem(window, IDCANCEL), TRUE);
        if (setup->error.empty()) {
            setup->ready = true;
            try {
                create_shortcuts(setup->destination,
                                 IsDlgButtonChecked(window, desktop_field) == BST_CHECKED,
                                 IsDlgButtonChecked(window, start_menu_field) == BST_CHECKED);
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(), "Game installed", MB_OK | MB_ICONWARNING);
            }
            SetDlgItemTextW(window, status_field,
                            L"Ready to play. The source disc or folder is no longer needed.");
            SetDlgItemTextW(window, IDOK, L"Play");
            SetDlgItemTextW(window, IDCANCEL, L"Close");
            EnableWindow(GetDlgItem(window, IDOK), TRUE);
            SetFocus(GetDlgItem(window, IDOK));
        } else {
            enable_fields(window, true);
            SetDlgItemTextW(window, status_field,
                            L"Setup did not finish. Check the details below before retrying.");
            MessageBoxA(window, setup->error.c_str(), "The X-Files Setup", MB_OK | MB_ICONERROR);
        }
        return TRUE;
    }
    if (message != WM_COMMAND && message != WM_CLOSE) {
        return FALSE;
    }
    try {
        const auto command = message == WM_CLOSE ? IDCANCEL : LOWORD(parameter);
        if (command == IDCANCEL) {
            if (setup->busy) {
                setup->cancel = true;
                SetDlgItemTextW(window, status_field, L"Stopping after the current file...");
                EnableWindow(GetDlgItem(window, IDCANCEL), FALSE);
            } else {
                EndDialog(window, IDCANCEL);
            }
            return TRUE;
        }
        if (setup->busy) {
            return TRUE;
        }
        if (command == source_browse || command == source_iso || command == destination_browse) {
            const auto title = command == source_iso ? L"Choose the English PC DVD ISO"
                               : command == source_browse
                                   ? L"Choose game files or all seven CD ISOs"
                                   : L"Choose a parent folder for The X-Files";
            auto path = browse(window, title, command == source_iso);
            if (!path.empty()) {
                if (command == destination_browse) {
                    path /= L"The X-Files";
                }
                SetDlgItemTextW(window,
                                command == destination_browse ? destination_field : source_field,
                                path.c_str());
            }
            return TRUE;
        }
        if (command == IDOK) {
            if (setup->ready) {
                play(setup->destination);
                EndDialog(window, IDOK);
            } else {
                start(window, *setup);
            }
            return TRUE;
        }
    } catch (const std::exception& error) {
        setup->busy = false;
        enable_fields(window, true);
        MessageBoxA(window, error.what(), "The X-Files Setup", MB_OK | MB_ICONERROR);
    }
    return FALSE;
}
}

void run_setup() {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        throw std::runtime_error("Cannot open Setup.");
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    Setup setup;
    const auto result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(105), nullptr,
                                        procedure, reinterpret_cast<LPARAM>(&setup));
    if (setup.worker.joinable()) {
        setup.cancel = true;
        setup.worker.join();
    }
    CoUninitialize();
    if (result == -1) {
        throw std::runtime_error("Cannot open the setup window.");
    }
}
