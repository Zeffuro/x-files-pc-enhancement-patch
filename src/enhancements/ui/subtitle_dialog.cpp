#include "subtitle_dialog.h"
#include "resources.h"
#include "media/subtitles.h"
#include "localization/ui.h"

#include <commctrl.h>
#include <commdlg.h>
#include <vector>
#include <shobjidl.h>
#include <atomic>
#include <stdexcept>
#include <thread>

namespace enhancements {
namespace {
struct Operation {
    std::filesystem::path game, folder;
    bool install, zip;
    std::atomic<bool> cancel{false}, done{false};
    std::atomic<std::size_t> completed{0}, total{0};
    std::thread worker;
    media::subtitles::Result result;
    std::string error;

    ~Operation() {
        cancel = true;
        if (worker.joinable()) {
            worker.join();
        }
    }
};

INT_PTR CALLBACK format_proc(HWND window, UINT message, WPARAM value, LPARAM data) {
    if (message == WM_INITDIALOG) {
        ui::translate_dialog(window);
        SetWindowTextW(window, ui::translate(data ? L"Install subtitles" : L"Export subtitles"));
        SetDlgItemTextW(
            window, IDC_SUBTITLE_STATUS,
            ui::translate(data ? L"Open a ZIP pack or its extracted folder."
                               : L"Use a ZIP to share subtitles, or a folder to edit them."));
        return TRUE;
    }
    if (message == WM_COMMAND &&
        (LOWORD(value) == IDYES || LOWORD(value) == IDNO || LOWORD(value) == IDCANCEL)) {
        EndDialog(window, LOWORD(value));
        return TRUE;
    }
    return FALSE;
}

std::filesystem::path choose_zip(HWND owner, bool install) {
    std::vector<wchar_t> path(32768);
    if (!install) {
        wcscpy_s(path.data(), path.size(), L"XFiles-subtitles.zip");
    }
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrFilter = L"Subtitle pack (*.zip)\0*.zip\0\0";
    dialog.lpstrDefExt = L"zip";
    dialog.lpstrTitle = ui::translate(install ? L"Install subtitle pack" : L"Export subtitle pack");
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                   (install ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
    if (!(install ? GetOpenFileNameW(&dialog) : GetSaveFileNameW(&dialog))) {
        if (CommDlgExtendedError()) {
            throw std::runtime_error("Cannot open subtitle file picker.");
        }
        return {};
    }
    return path.data();
}

std::filesystem::path choose_folder(HWND owner, bool install) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        throw std::runtime_error("Cannot open the folder picker");
    }

    struct Cleanup {
        bool initialized;
        IFileOpenDialog* dialog = nullptr;
        IShellItem* item = nullptr;
        PWSTR name = nullptr;

        ~Cleanup() {
            CoTaskMemFree(name);
            if (item) {
                item->Release();
            }
            if (dialog) {
                dialog->Release();
            }
            if (initialized) {
                CoUninitialize();
            }
        }
    } cleanup{SUCCEEDED(initialized)};

    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&cleanup.dialog)))) {
        throw std::runtime_error("Cannot open the folder picker");
    }
    if (FAILED(cleanup.dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                          FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR)) ||
        FAILED(cleanup.dialog->SetTitle(
            ui::translate(install ? L"Choose the subtitle pack folder"
                                  : L"Choose an empty folder for subtitles")))) {
        throw std::runtime_error("Cannot configure the folder picker");
    }
    const auto shown = cleanup.dialog->Show(owner);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return {};
    }
    if (FAILED(shown) || FAILED(cleanup.dialog->GetResult(&cleanup.item)) ||
        FAILED(cleanup.item->GetDisplayName(SIGDN_FILESYSPATH, &cleanup.name))) {
        throw std::runtime_error("Cannot read the selected folder");
    }
    return cleanup.name;
}

INT_PTR CALLBACK operation_proc(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    auto* operation = reinterpret_cast<Operation*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        operation = reinterpret_cast<Operation*>(data);
        SetWindowLongPtrW(window, DWLP_USER, data);
        ui::translate_dialog(window);
        SetWindowTextW(
            window, ui::translate(operation->install ? L"Install subtitles" : L"Export subtitles"));
        SetDlgItemTextW(window, IDC_SUBTITLE_STATUS, ui::translate(L"Checking movies..."));
        SendDlgItemMessageW(window, IDC_SUBTITLE_PROGRESS, PBM_SETRANGE32, 0, 100);
        if (!SetTimer(window, 1, 100, nullptr)) {
            operation->error = "Cannot start subtitle progress updates";
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        try {
            operation->worker = std::thread([operation] {
                try {
                    const auto progress = [operation](std::size_t completed, std::size_t total) {
                        operation->completed = completed;
                        operation->total = total;
                        return !operation->cancel;
                    };
                    const auto action = operation->install
                                            ? (operation->zip ? media::subtitles::install_zip
                                                              : media::subtitles::install_pack)
                                            : (operation->zip ? media::subtitles::export_zip
                                                              : media::subtitles::export_pack);
                    operation->result = action(operation->game, operation->folder, progress);
                } catch (const std::exception& error) {
                    operation->error = error.what();
                } catch (...) {
                    operation->error = "The subtitle operation could not finish";
                }
                operation->done = true;
            });
        } catch (const std::exception& error) {
            operation->error = error.what();
            KillTimer(window, 1);
            EndDialog(window, IDCANCEL);
        }
        return TRUE;
    }
    if (!operation) {
        return FALSE;
    }
    if (message == WM_TIMER) {
        if (operation->done) {
            operation->worker.join();
            KillTimer(window, 1);
            EndDialog(window, IDOK);
            return TRUE;
        }
        if (!operation->cancel) {
            const auto completed = operation->completed.load();
            const auto total = operation->total.load();
            const auto text = std::wstring(ui::translate(L"Processed: ")) +
                              std::to_wstring(completed) + ui::translate(L" of ") +
                              std::to_wstring(total);
            SetDlgItemTextW(window, IDC_SUBTITLE_STATUS, text.c_str());
            SendDlgItemMessageW(window, IDC_SUBTITLE_PROGRESS, PBM_SETPOS,
                                total ? completed * 100 / total : 0, 0);
        }
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(parameter) == IDCANCEL)) {
        operation->cancel = true;
        EnableWindow(GetDlgItem(window, IDCANCEL), FALSE);
        SetDlgItemTextW(window, IDC_SUBTITLE_STATUS, ui::translate(L"Cancelling..."));
        return TRUE;
    }
    return FALSE;
}
}

void subtitle_dialog(HWND owner, HMODULE module, const std::filesystem::path& directory,
                     bool install) {
    const auto format =
        DialogBoxParamW(module, MAKEINTRESOURCEW(IDD_SUBTITLE_FORMAT), owner, format_proc, install);
    if (format == -1) {
        throw std::runtime_error("Cannot open subtitle format options.");
    }
    if (format != IDYES && format != IDNO) {
        return;
    }
    const bool zip = format == IDYES;
    const auto folder = zip ? choose_zip(owner, install) : choose_folder(owner, install);
    if (folder.empty()) {
        return;
    }
    Operation operation{directory, folder, install, zip};
    const auto result = DialogBoxParamW(module, MAKEINTRESOURCEW(IDD_SUBTITLE_PROGRESS), owner,
                                        operation_proc, reinterpret_cast<LPARAM>(&operation));
    if (result == -1) {
        throw std::runtime_error("Cannot open subtitle progress");
    }
    if (operation.cancel) {
        return;
    }
    if (!operation.error.empty()) {
        throw std::runtime_error(operation.error);
    }
    std::wstring text =
        ui::translate(install ? L"Subtitles installed. Set closed captions to On to show them."
                              : L"Subtitles exported as SRT files. Edit the text, then use Install "
                                L"subtitles to load the pack.");
    if (operation.result.skipped) {
        text += std::wstring(ui::translate(L"\n\nSkipped ")) +
                std::to_wstring(operation.result.skipped) +
                ui::translate(L" unreadable movies. See skipped.txt in the exported pack.");
    }
    MessageBoxW(owner, text.c_str(), ui::translate(L"The X-Files subtitles"),
                MB_OK | MB_ICONINFORMATION);
}
}
