#include "process.h"
#include "platform/desktop.h"
#include "platform/cursor.h"
#include "platform/handle.h"

#include <fstream>
#include <optional>
#include <stdexcept>

namespace {

[[noreturn]] void win_error(const char* operation) {
    throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(GetLastError()));
}

struct Snapshot {
    Desktop desktop;
    CursorClip clip;
};

std::optional<Snapshot> snapshot(std::ostream& report, const char* label, bool required) {
    report << label << '\n';
    try {
        Snapshot value{desktop_layout(), cursor_clip()};
        write_desktop(report, value.desktop);
        report << "Cursor clip: " << value.clip.left << ',' << value.clip.top << " to "
               << value.clip.right << ',' << value.clip.bottom << '\n'
               << std::flush;
        return value;
    } catch (const std::exception& error) {
        report << "Desktop snapshot unavailable: " << error.what() << '\n' << std::flush;
        if (required) {
            throw;
        }
        return std::nullopt;
    }
}

}

RunResult run_game(const std::filesystem::path& directory, bool portable, bool probe) {
    Handle instance(CreateMutexW(nullptr, TRUE, L"Local\\XFilesEnhancementGame"));
    const auto instance_error = GetLastError();
    if (!instance.get()) {
        win_error("Creating game instance guard");
    }
    if (instance_error == ERROR_ALREADY_EXISTS) {
        throw std::runtime_error(
            "A patched game is already running. Close it before starting another.");
    }
    const auto preferences = directory / L"preferences.ini";
    if (!SetEnvironmentVariableW(L"XFILES_PATCH_DISPLAY", L"1")) {
        win_error("Setting the logical display mode");
    }
    if (!SetEnvironmentVariableW(L"XFILES_PATCH_PREFERENCES",
                                 portable ? preferences.c_str() : nullptr)) {
        win_error("Setting portable preferences");
    }
    const auto log = directory / L"quicktime.log";
    if (!SetEnvironmentVariableW(L"XFILES_PATCH_LOG", log.c_str())) {
        win_error("SetEnvironmentVariable");
    }
    const auto display_config = directory / L"ddraw.ini";
    if (!SetEnvironmentVariableW(L"CNC_DDRAW_CONFIG_FILE", display_config.c_str())) {
        win_error("SetEnvironmentVariable");
    }

    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.get()) {
        win_error("CreateJobObject");
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits))) {
        win_error("SetInformationJobObject");
    }

    std::ofstream report(directory / L"desktop.log");
    report << "patch=" XFILES_BUILD_VERSION "\nprobe=" << probe << '\n';
    const auto before = snapshot(report, "Before launch", probe);

    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = probe ? SW_HIDE : SW_SHOWNORMAL;
    PROCESS_INFORMATION process{};
    const auto executable = directory / L"XFiles.exe";
    std::wstring command = L"\"" + executable.wstring() + L"\"";
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &process)) {
        win_error("CreateProcess");
    }

    Handle child(process.hProcess);
    Handle thread(process.hThread);
    if (!AssignProcessToJobObject(job.get(), child.get())) {
        const DWORD error = GetLastError();
        TerminateProcess(child.get(), 1);
        WaitForSingleObject(child.get(), 5000);
        SetLastError(error);
        win_error("AssignProcessToJobObject");
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        win_error("ResumeThread");
    }

    RunResult result{};
    if (!probe) {
        // Desktop and cursor state are shared with other applications.
        if (WaitForSingleObject(child.get(), INFINITE) != WAIT_OBJECT_0) {
            win_error("WaitForSingleObject");
        }
    } else {
        const auto deadline = GetTickCount64() + 15000;
        for (;;) {
            const DWORD wait = WaitForSingleObject(child.get(), 25);
            if (wait == WAIT_OBJECT_0) {
                break;
            }
            if (wait != WAIT_TIMEOUT) {
                win_error("WaitForSingleObject");
            }
            result.desktop_changed = desktop_layout() != before->desktop;
            result.cursor_changed = cursor_clip() != before->clip;
            result.timed_out = GetTickCount64() >= deadline;
            if (result.desktop_changed || result.cursor_changed || result.timed_out) {
                report << "Probe stopped: desktop_changed=" << result.desktop_changed
                       << " cursor_changed=" << result.cursor_changed
                       << " timeout=" << result.timed_out << '\n'
                       << std::flush;
                if (!TerminateJobObject(job.get(), result.timed_out ? 0x58510002 : 0x58510003)) {
                    win_error("TerminateJobObject");
                }
                if (WaitForSingleObject(child.get(), 5000) != WAIT_OBJECT_0) {
                    throw std::runtime_error("The game did not stop.");
                }
                break;
            }
        }
    }

    const auto after = snapshot(report, "After exit", probe);
    if (before && after) {
        result.desktop_changed |= after->desktop != before->desktop;
        result.cursor_changed |= after->clip != before->clip;
    }
    report << "changed=" << result.desktop_changed << "\ncursor_changed=" << result.cursor_changed
           << '\n';

    if (!GetExitCodeProcess(child.get(), &result.exit_code)) {
        win_error("GetExitCodeProcess");
    }
    return result;
}
