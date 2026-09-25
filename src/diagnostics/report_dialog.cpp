#include "report_dialog.h"
#include "report.h"

#include <commdlg.h>
#include <shellapi.h>
#include <stdexcept>
#include <vector>

namespace diagnostics {
namespace {
std::filesystem::path choose(HWND owner, bool writing, bool zip,
                             const std::filesystem::path& directory) {
    std::vector<wchar_t> buffer(32768);
    if (writing) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        swprintf_s(buffer.data(), buffer.size(), L"XFiles-%s-%04u%02u%02u-%02u%02u%02u.%s",
                   zip ? L"report" : L"save", time.wYear, time.wMonth, time.wDay, time.wHour,
                   time.wMinute, time.wSecond, zip ? L"zip" : L"x");
    }
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrInitialDir = directory.c_str();
    dialog.lpstrFilter =
        zip ? L"Troubleshooting ZIP\0*.zip\0\0" : L"X-Files PC saved game\0*.x\0\0";
    dialog.lpstrDefExt = zip ? L"zip" : L"x";
    dialog.lpstrTitle = zip       ? L"Save troubleshooting report"
                        : writing ? L"Save game to file"
                                  : L"Choose saved game";
    dialog.Flags =
        OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_EXPLORER | (writing ? 0 : OFN_FILEMUSTEXIST);
    if (writing ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) {
        return buffer.data();
    }
    if (CommDlgExtendedError()) {
        throw std::runtime_error("Cannot open the file chooser");
    }
    return {};
}
}

std::filesystem::path choose_save_file(HWND owner, bool writing,
                                       const std::filesystem::path& directory) {
    return choose(owner, writing, false, directory);
}

void save_report_dialog(HWND owner, const std::filesystem::path& directory,
                        std::string_view details, bool include_save) {
    std::filesystem::path save;
    if (include_save) {
        save = choose_save_file(owner, false, directory);
        if (save.empty()) {
            return;
        }
    }
    const auto output = choose(owner, true, true, directory);
    if (output.empty()) {
        return;
    }
    create_report(directory, output, details, save);
    MessageBoxW(owner, L"Report saved. Nothing was uploaded. Review the ZIP before sharing it.",
                L"The X-Files", MB_OK | MB_ICONINFORMATION);
}

void show_crash_report(const std::filesystem::path& directory, unsigned exit_code) {
    const auto result =
        MessageBoxW(nullptr,
                    L"The game stopped unexpectedly. Diagnostic logs have been kept in the game "
                    L"folder.\n\nCreate a troubleshooting ZIP now?",
                    L"The X-Files", MB_YESNO | MB_ICONERROR);
    if (result != IDYES) {
        return;
    }
    const bool include_save =
        MessageBoxW(nullptr,
                    L"Include a saved game to help reproduce the problem?\n\nYou can choose one "
                    L"existing save. The game cannot recover unsaved progress after a crash.",
                    L"Troubleshooting report", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
    try {
        save_report_dialog(
            nullptr, directory,
            "The X-Files enhancement " XFILES_BUILD_VERSION "\nUnexpected exit: " +
                std::to_string(exit_code) +
                "\nSee crash.log for edition and executable identity.\n"
                "Logs may contain local paths. A save is included only if selected.\n",
            include_save);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Cannot create report", MB_OK | MB_ICONERROR);
    }
}
}
