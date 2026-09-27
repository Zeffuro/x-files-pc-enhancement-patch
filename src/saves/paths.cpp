#include "paths.h"

#include <windows.h>
#include <vector>

namespace saves {
std::filesystem::path redirected_path(const std::filesystem::path& directory,
                                      const std::filesystem::path& requested) {
    if (requested.empty() || _wcsicmp(requested.extension().c_str(), L".x")) {
        return {};
    }
    const auto absolute = std::filesystem::absolute(requested).lexically_normal();
    const auto game = std::filesystem::absolute(directory).lexically_normal().parent_path();
    if (_wcsicmp(absolute.parent_path().c_str(), game.c_str())) {
        return {};
    }
    return directory / absolute.filename();
}

std::filesystem::path redirected_path(const char* requested) noexcept {
    try {
        if (!requested) {
            return {};
        }
        std::vector<wchar_t> folder(32768);
        const auto length = GetEnvironmentVariableW(L"XFILES_PATCH_SAVES", folder.data(),
                                                    static_cast<DWORD>(folder.size()));
        if (!length || length >= folder.size()) {
            return {};
        }
        return redirected_path(folder.data(), requested);
    } catch (...) {
        return {};
    }
}
}
