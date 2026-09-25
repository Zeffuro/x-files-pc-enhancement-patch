#include "application.h"
#include "configuration.h"
#include "process.h"
#include "runtime.h"
#include "preferences/store.h"
#include "diagnostics/log_file.h"
#include "diagnostics/report_dialog.h"

#include <fstream>
#include <stdexcept>

std::filesystem::path application_directory() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) {
        throw std::runtime_error("Cannot locate the launcher.");
    }
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

int execute_game(const StagedGame& game, const Session& session, bool diagnostic) {
    if (!SetEnvironmentVariableW(L"XFILES_PATCH_MEDIA", session.media.c_str())) {
        throw std::runtime_error("Cannot configure the game files.");
    }
    const auto result = run_game(game.directory, session.portable, diagnostic);
    std::ofstream report(game.directory / L"launcher.log");
    report << "patch=" << XFILES_BUILD_VERSION << "\nedition=" << game.identity.edition
           << "\nsha256=" << game.identity.sha256 << "\nexit=0x" << std::hex << result.exit_code
           << "\ntimeout=" << result.timed_out << "\ndesktop_changed=" << result.desktop_changed
           << "\ncursor_changed=" << result.cursor_changed << '\n';
    report.close();
    if (result.exit_code != 0 && !result.timed_out) {
        diagnostics::preserve_crash_logs(game.directory);
    }
    if (diagnostic) {
        if (result.exit_code == unsupported_exit) {
            return 2;
        }
        return result.exit_code == 0 && !result.desktop_changed && !result.cursor_changed &&
                       !result.timed_out
                   ? 0
                   : 3;
    }
    if (result.exit_code != 0) {
        diagnostics::show_crash_report(game.directory, result.exit_code);
        return 1;
    }
    return 0;
}

int resume_game(const std::filesystem::path& directory) {
    const auto game = std::filesystem::canonical(directory);
    if (std::filesystem::exists(game / L"installing.txt")) {
        throw std::runtime_error("Setup did not finish. Run Setup again using a new folder.");
    }
    const auto identity = identify(game / L"XFiles.exe");
    if (!identity.edition) {
        throw std::runtime_error("This game executable is not supported. Run Setup with an "
                                 "English PC CD or DVD copy.");
    }
    const auto session = read_session(game);
    initialize_configuration(game);
    if (session.portable) {
        preferences::Store store(game / L"preferences.ini");
        store.set("Exe Full Path", (game / L"XFiles.exe").string());
    }
    return execute_game({game, identity}, session);
}
