#include "log_file.h"

#include <algorithm>
#include <array>
#include <utility>
#include <fstream>

namespace diagnostics {
bool copy_log_tail(const std::filesystem::path& source_path,
                   const std::filesystem::path& target_path) noexcept {
    try {
        const auto size = std::filesystem::file_size(source_path);
        std::ifstream source(source_path, std::ios::binary);
        if (!source) {
            return false;
        }
        std::ofstream target(target_path, std::ios::binary | std::ios::trunc);
        if (size > log_limit) {
            source.seekg(static_cast<std::streamoff>(size - log_limit));
        }
        if (!source || !target) {
            return false;
        }
        std::array<char, 4096> buffer{};
        auto remaining = std::min(size, log_limit);
        while (remaining) {
            const auto count = static_cast<std::streamsize>(
                std::min(remaining, static_cast<std::uintmax_t>(buffer.size())));
            if (!source.read(buffer.data(), count) || !target.write(buffer.data(), count)) {
                return false;
            }
            remaining -= static_cast<std::uintmax_t>(count);
        }
        target.close();
        return bool(target);
    } catch (...) {
        return false;
    }
}

void preserve_crash_logs(const std::filesystem::path& directory) noexcept {
    try {
        for (const auto& [source, target] : {std::pair{"launcher.log", "crash.log"},
                                             std::pair{"quicktime.log", "crash-quicktime.log"},
                                             std::pair{"desktop.log", "crash-desktop.log"}}) {
            if (!copy_log_tail(directory / source, directory / target)) {
                // Do not mix this crash with a snapshot from an older session.
                std::error_code ignored;
                std::filesystem::remove(directory / target, ignored);
            }
        }
    } catch (...) {
        // Failed diagnostics must not replace the original crash report.
    }
}

bool append_log(const std::filesystem::path& path, std::string_view line) noexcept {
    try {
        if (line.size() > log_limit) {
            return false;
        }
        const auto size = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
        if (size + line.size() > log_limit) {
            if (!copy_log_tail(path, path.wstring() + L".previous")) {
                return false;
            }
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(line.data(), static_cast<std::streamsize>(line.size()));
            output.close();
            return bool(output);
        }
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output.write(line.data(), static_cast<std::streamsize>(line.size()));
        output.close();
        return bool(output);
    } catch (...) {
        return false;
    }
}
}
