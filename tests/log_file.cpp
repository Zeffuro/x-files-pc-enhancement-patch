#include "diagnostics/log_file.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <utility>

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("xfiles-log-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(root)) {
        return 5;
    }

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{root};

    const auto path = root / "test.log";
    const auto backup = root / "test.log.previous";
    const std::string record(8192, 'x');
    for (unsigned i = 0; i < 600; ++i) {
        if (!diagnostics::append_log(path, record)) {
            return 1;
        }
    }
    if (std::filesystem::file_size(path) > diagnostics::log_limit ||
        std::filesystem::file_size(backup) > diagnostics::log_limit) {
        return 2;
    }
    {
        std::ofstream old(path, std::ios::binary | std::ios::trunc);
        old << std::string(diagnostics::log_limit + 12345, 'a');
    }
    const auto crash = root / "crash.log";
    if (!diagnostics::copy_log_tail(path, crash) ||
        std::filesystem::file_size(crash) != diagnostics::log_limit) {
        return 4;
    }
    if (!diagnostics::append_log(path, "latest") || std::filesystem::file_size(path) != 6 ||
        std::filesystem::file_size(backup) != diagnostics::log_limit ||
        diagnostics::append_log(root / "missing" / "test.log", "failure")) {
        return 3;
    }
    std::ofstream(root / "quicktime.log.previous") << "older";
    const auto logs = diagnostics::prepare_log_directory(root);
    if (!std::filesystem::exists(logs / "legacy" / "quicktime.log.previous") ||
        std::filesystem::exists(root / "quicktime.log.previous")) {
        return 9;
    }
    std::ofstream(logs / "launcher.log") << "launcher";
    std::ofstream(root / "launcher.log") << "legacy";
    diagnostics::prepare_log_directory(root);
    if (!std::filesystem::exists(logs / "legacy" / "launcher.log") ||
        std::filesystem::exists(root / "launcher.log")) {
        return 10;
    }
    std::ofstream(logs / "quicktime.log") << "quicktime";
    std::ofstream(logs / "desktop.log") << "desktop";
    diagnostics::preserve_crash_logs(root);
    for (const auto& [name, expected] :
         {std::pair{"crash.log", "launcher"}, std::pair{"crash-quicktime.log", "quicktime"},
          std::pair{"crash-desktop.log", "desktop"}}) {
        std::ifstream input(logs / name);
        std::string actual;
        input >> actual;
        if (actual != expected) {
            return 6;
        }
    }
    std::filesystem::remove(logs / "desktop.log");
    diagnostics::preserve_crash_logs(root);
    if (std::filesystem::exists(logs / "crash-desktop.log")) {
        return 7;
    }
    std::ofstream(root / "empty.log");
    if (!diagnostics::copy_log_tail(root / "empty.log", root / "empty-copy.log") ||
        std::filesystem::file_size(root / "empty-copy.log") != 0 ||
        diagnostics::copy_log_tail(path, root / "missing" / "copy.log")) {
        return 8;
    }
    std::cout << "Log bounds, rotation, crash snapshots and write failures passed.\n";
}
