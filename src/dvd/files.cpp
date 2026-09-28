#include "files.h"
#include "platform/imports.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace dvd {
namespace {
ImportHooks hooks;
unsigned users = 0;

HFILE WINAPI open_file(LPCSTR path, LPOFSTRUCT info, UINT flags) {
    const auto result = OpenFile(path, info, flags);
    const auto error = GetLastError();
    if (result == HFILE_ERROR && flags == OF_EXIST && path) {
        try {
            const auto mapped = movie_path(path);
            if (!mapped.empty()) {
                return OpenFile(mapped.string().c_str(), info, flags);
            }
        } catch (...) {
        }
    }
    SetLastError(error);
    return result;
}

FARPROC resolve(const char* name) {
    return std::strcmp(name, "OpenFile") ? nullptr : reinterpret_cast<FARPROC>(open_file);
}
}

std::filesystem::path movie_path(const std::filesystem::path& requested) {
    if (std::filesystem::is_regular_file(requested)) {
        return requested;
    }
    if (_wcsicmp(requested.parent_path().filename().c_str(), L"vob") ||
        _wcsicmp(requested.extension().c_str(), L".vob")) {
        return {};
    }
    std::vector<wchar_t> name(32768);
    const auto size = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
    if (!size || size >= name.size()) {
        return {};
    }
    const auto local =
        std::filesystem::path(name.data()).parent_path() / L"vob" / requested.filename();
    return std::filesystem::is_regular_file(local) ? local : std::filesystem::path{};
}

void acquire_file_hook() {
    // Portable installations have no optical-drive root for the native existence check.
    if (!users && !hooks.install(GetModuleHandleW(nullptr), "kernel32.dll", resolve)) {
        throw std::runtime_error("Cannot map native DVD file checks");
    }
    ++users;
}

void release_file_hook() {
    if (users && !--users) {
        hooks.remove();
    }
}
}
