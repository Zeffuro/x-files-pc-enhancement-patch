#include "diagnostics/report.h"
#include "diagnostics/log_file.h"

#include <windows.h>
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return 1;
    }
    const std::filesystem::path root = argv[1];
    std::filesystem::create_directories(root);
    std::ofstream(root / "quicktime.log", std::ios::binary)
        << std::string(diagnostics::log_limit + 19, 'x') << "tail";
    std::ofstream(root / "launcher.log") << "launcher evidence";
    std::ofstream(root / "desktop.log", std::ios::binary)
        << std::string(diagnostics::log_limit + 9, 'd') << "desktop tail";
    std::ofstream(root / "crash-desktop.log") << "previous crash desktop";
    std::ofstream(root / "PRIVATE.x") << "save must not be included";
    std::ofstream(root / "preferences.ini") << "private preferences";
    const auto zip = root / "report.zip";
    diagnostics::create_report(root, zip, "test summary");
    try {
        diagnostics::create_report(root, zip, "must not overwrite");
        return 2;
    } catch (const std::exception&) {
    }
    std::string save(128, '\0');
    save[3] = 5;
    save[22] = 5;
    save[23] = 1;
    std::ofstream(root / "selected.x", std::ios::binary).write(save.data(), save.size());
    diagnostics::create_report(root, root / "with-save.zip", "save selected", root / "selected.x");
    try {
        diagnostics::create_report(root, root / "invalid.zip", "invalid", root / "PRIVATE.x");
        return 3;
    } catch (const std::exception&) {
    }
    if (std::filesystem::exists(root / "invalid.zip")) {
        return 4;
    }
    std::cout << "Report written; existing destination protected.\n";
    return 0;
}
