#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <filesystem>
#include <system_error>

namespace platform {
inline bool copy_file(const std::filesystem::path& source, const std::filesystem::path& destination,
                      std::filesystem::copy_options options = std::filesystem::copy_options::none) {
    namespace fs = std::filesystem;
    const bool skip = options == fs::copy_options::skip_existing;
    const bool overwrite = options == fs::copy_options::overwrite_existing;
    if (options != fs::copy_options::none && !skip && !overwrite) {
        throw fs::filesystem_error("Unsupported copy option", source, destination,
                                   std::make_error_code(std::errc::invalid_argument));
    }
#ifdef _WIN32
    if (CopyFileW(source.c_str(), destination.c_str(), overwrite ? FALSE : TRUE)) {
        return true;
    }
    const auto error = GetLastError();
    if (skip && (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)) {
        return false;
    }
    throw fs::filesystem_error("Cannot copy file", source, destination,
                               std::error_code(static_cast<int>(error), std::system_category()));
#else
    return fs::copy_file(source, destination, options);
#endif
}
}
