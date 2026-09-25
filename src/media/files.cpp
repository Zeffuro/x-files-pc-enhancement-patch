#include "files.h"
#include "platform/imports.h"
#include "runtime.h"

#include <cstring>
#include <vector>

namespace media {
namespace {

ImportHooks hooks;

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
    const auto result =
        CreateFileA(path, access, sharing, security, creation, flags, template_file);
    const auto error = GetLastError();
    constexpr DWORD writable = GENERIC_WRITE | GENERIC_ALL | DELETE | WRITE_DAC | WRITE_OWNER |
                               FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
                               FILE_WRITE_ATTRIBUTES;
    if (result != INVALID_HANDLE_VALUE || !missing_file(error) || creation != OPEN_EXISTING ||
        (access & writable) || (flags & FILE_FLAG_DELETE_ON_CLOSE)) {
        SetLastError(error);
        return result;
    }
    const auto mapped = mapped_file(path);
    if (!mapped.empty()) {
        return CreateFileW(mapped.c_str(), access, sharing, security, creation, flags,
                           template_file);
    }
    SetLastError(error);
    return result;
}

HANDLE WINAPI find_file(const char* path, WIN32_FIND_DATAA* data) {
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

FARPROC resolve(const char* name) {
    if (!std::strcmp(name, "CreateFileA")) {
        return reinterpret_cast<FARPROC>(open_file);
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
    return hooks.install(executable, "kernel32.dll", resolve);
}

void remove_file_hooks() {
    hooks.remove();
}

}
