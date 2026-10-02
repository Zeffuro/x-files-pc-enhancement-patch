#include "files.h"
#include "platform/imports.h"
#include "runtime.h"
#include "saves/paths.h"
#include "game/database/io.h"

#include <cstring>
#include <vector>
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace media {
namespace {

ImportHooks hooks;

template <class Function> Function original(Function hook, Function fallback) {
    const auto previous = hooks.previous(reinterpret_cast<FARPROC>(hook));
    return previous ? reinterpret_cast<Function>(previous) : fallback;
}

HANDLE observed_open(HANDLE result, const char* requested, DWORD flags) {
    game_assets::observe_database_open(result, requested, flags);
    return result;
}

BOOL WINAPI read_file(HANDLE handle, void* buffer, DWORD requested, DWORD* transferred,
                      OVERLAPPED* overlapped) {
#ifdef _MSC_VER
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
#else
    const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
#endif
    const auto token = game_assets::begin_database_read(handle, overlapped);
    const auto result =
        original(read_file, ReadFile)(handle, buffer, requested, transferred, overlapped);
    const auto error = GetLastError();
    game_assets::end_database_read(token, result, result && transferred ? *transferred : 0, caller);
    SetLastError(error);
    return result;
}

BOOL WINAPI close_file(HANDLE handle) {
    const auto generation = game_assets::begin_database_close(handle);
    const auto result = original(close_file, CloseHandle)(handle);
    const auto error = GetLastError();
    game_assets::observe_database_close(handle, result, generation);
    SetLastError(error);
    return result;
}

bool missing_file(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

std::filesystem::path mapped_file(const char* path) noexcept {
    try {
        return path ? locate_file(path) : std::filesystem::path{};
    } catch (...) {
        return {};
    }
}

HANDLE WINAPI open_file(const char* path, DWORD access, DWORD sharing,
                        SECURITY_ATTRIBUTES* security, DWORD creation, DWORD flags,
                        HANDLE template_file) {
    const auto save = saves::redirected_path(path);
    if (!save.empty()) {
        return observed_open(
            CreateFileW(save.c_str(), access, sharing, security, creation, flags, template_file),
            path, flags);
    }
    const auto result = original(open_file, CreateFileA)(path, access, sharing, security, creation,
                                                         flags, template_file);
    const auto error = GetLastError();
    constexpr DWORD writable = GENERIC_WRITE | GENERIC_ALL | DELETE | WRITE_DAC | WRITE_OWNER |
                               FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
                               FILE_WRITE_ATTRIBUTES;
    if (result != INVALID_HANDLE_VALUE || !missing_file(error) || creation != OPEN_EXISTING ||
        (access & writable) || (flags & FILE_FLAG_DELETE_ON_CLOSE)) {
        SetLastError(error);
        return observed_open(result, path, flags);
    }
    const auto mapped = mapped_file(path);
    if (!mapped.empty()) {
        return observed_open(
            CreateFileW(mapped.c_str(), access, sharing, security, creation, flags, template_file),
            path, flags);
    }
    SetLastError(error);
    return result;
}

HANDLE WINAPI find_file(const char* path, WIN32_FIND_DATAA* data) {
    const auto save = saves::redirected_path(path);
    if (!save.empty()) {
        return FindFirstFileA(save.string().c_str(), data);
    }
    const auto result = FindFirstFileA(path, data);
    const auto error = GetLastError();
    if (result == INVALID_HANDLE_VALUE && missing_file(error)) {
        const auto mapped = mapped_file(path);
        if (!mapped.empty()) {
            return FindFirstFileA(mapped.string().c_str(), data);
        }
    }
    SetLastError(error);
    return result;
}

DWORD WINAPI attributes(const char* path) {
    const auto save = saves::redirected_path(path);
    if (!save.empty()) {
        return GetFileAttributesW(save.c_str());
    }
    const auto result = GetFileAttributesA(path);
    const auto error = GetLastError();
    if (result == INVALID_FILE_ATTRIBUTES && missing_file(error)) {
        const auto mapped = mapped_file(path);
        if (!mapped.empty()) {
            return GetFileAttributesW(mapped.c_str());
        }
    }
    SetLastError(error);
    return result;
}

BOOL WINAPI delete_file(const char* path) {
    const auto save = saves::redirected_path(path);
    return save.empty() ? DeleteFileA(path) : DeleteFileW(save.c_str());
}

FARPROC resolve(const char* name) {
    if (!std::strcmp(name, "DeleteFileA")) {
        return reinterpret_cast<FARPROC>(delete_file);
    }
    if (!std::strcmp(name, "CreateFileA")) {
        return reinterpret_cast<FARPROC>(open_file);
    }
    if (!std::strcmp(name, "ReadFile")) {
        return reinterpret_cast<FARPROC>(read_file);
    }
    if (!std::strcmp(name, "CloseHandle")) {
        return reinterpret_cast<FARPROC>(close_file);
    }
    if (!std::strcmp(name, "FindFirstFileA")) {
        return reinterpret_cast<FARPROC>(find_file);
    }
    if (!std::strcmp(name, "GetFileAttributesA")) {
        return reinterpret_cast<FARPROC>(attributes);
    }
    return nullptr;
}

}

std::filesystem::path locate_file(const std::filesystem::path& requested) {
    if (std::filesystem::is_regular_file(requested)) {
        return requested;
    }
    std::vector<wchar_t> buffer(32768);
    const auto length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) {
        return {};
    }
    const auto game = std::filesystem::path(buffer.data()).parent_path();
    const auto absolute = std::filesystem::absolute(requested).lexically_normal();
    auto component = absolute.begin();
    for (const auto& parent : game) {
        if (component == absolute.end() || _wcsicmp(component->c_str(), parent.c_str())) {
            return {};
        }
        ++component;
    }
    std::filesystem::path relative;
    for (; component != absolute.end(); ++component) {
        relative /= *component;
    }
    if (relative.empty()) {
        return {};
    }
    const auto size = GetEnvironmentVariableW(L"XFILES_PATCH_MEDIA", buffer.data(),
                                              static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) {
        return {};
    }
    const std::filesystem::path root(buffer.data());
    for (const auto* layer : {L"", L"MININST", L"MEDINST"}) {
        const auto candidate = root / layer / relative;
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool install_file_hooks(HMODULE executable) {
    game_assets::database_io_executable(executable);
    return hooks.install(executable, "kernel32.dll", resolve);
}

void remove_file_hooks() {
    hooks.remove();
}

}
