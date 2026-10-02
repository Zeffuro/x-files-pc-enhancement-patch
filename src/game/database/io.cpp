#include "io.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <cstring>

namespace game_assets {
namespace {

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    std::uint64_t generation = 0;
    bool current = false;
    bool asynchronous = false;
};

SRWLOCK mutex = SRWLOCK_INIT;
std::array<Handle, 8> handles;
std::array<wchar_t, 32768> path{};
std::array<wchar_t, 32768> resolved{};
std::array<DatabaseIoRead, 64> reads{};
std::uint64_t generation = 0, session = 0, total_reads = 0, total_bytes = 0;
std::uintptr_t executable_base = 0, executable_end = 0;

struct Lock {
    Lock() noexcept {
        AcquireSRWLockExclusive(&mutex);
    }

    ~Lock() {
        ReleaseSRWLockExclusive(&mutex);
    }
};

struct LastError {
    DWORD value = GetLastError();

    ~LastError() {
        SetLastError(value);
    }
};

bool database_name(const char* requested) noexcept {
    if (!requested) {
        return false;
    }
    const char* name = requested;
    for (const char* part = requested; *part; ++part) {
        if (*part == '/' || *part == '\\') {
            name = part + 1;
        }
    }
    return !_stricmp(name, "XFILES.HDB");
}

Handle* find(HANDLE value) noexcept {
    for (auto& handle : handles) {
        if (handle.value == value) {
            return &handle;
        }
    }
    return nullptr;
}

}

void database_io_executable(HMODULE executable) noexcept {
    LastError preserved;
    Lock locked;
    executable_base = reinterpret_cast<std::uintptr_t>(executable);
    executable_end = executable_base;
    if (executable) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(executable);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const auto* pe =
                reinterpret_cast<const IMAGE_NT_HEADERS*>(executable_base + dos->e_lfanew);
            if (pe->Signature == IMAGE_NT_SIGNATURE) {
                executable_end += pe->OptionalHeader.SizeOfImage;
            }
        }
    }
}

void observe_database_open(HANDLE value, const char* requested, DWORD flags) noexcept {
    LastError preserved;
    if (value == INVALID_HANDLE_VALUE || !value || !database_name(requested)) {
        return;
    }
    Lock locked;
    const DWORD length =
        GetFinalPathNameByHandleW(value, resolved.data(), static_cast<DWORD>(resolved.size()), 0);
    if (!length || length >= resolved.size()) {
        return;
    }
    const wchar_t* name = resolved.data();
    for (const wchar_t* part = name; *part; ++part) {
        if (*part == L'/' || *part == L'\\') {
            name = part + 1;
        }
    }
    if (_wcsicmp(name, L"XFILES.HDB")) {
        return;
    }
    Handle* slot = find(value);
    if (!slot) {
        slot = find(INVALID_HANDLE_VALUE);
    }
    if (!slot) {
        return;
    }
    const bool same = !_wcsicmp(path.data(), resolved.data());
    for (auto& handle : handles) {
        handle.current = same && handle.current;
    }
    std::copy_n(resolved.data(), length + 1, path.data());
    *slot = {value, ++generation, true, (flags & FILE_FLAG_OVERLAPPED) != 0};
    ++session;
    total_reads = total_bytes = 0;
    reads.fill({});
}

DatabaseIoReadToken begin_database_read(HANDLE value, OVERLAPPED* overlapped) noexcept {
    LastError preserved;
    Lock locked;
    const auto* handle = find(value);
    if (!handle || !handle->current || handle->asynchronous || overlapped) {
        return {};
    }
    LARGE_INTEGER zero{}, position{};
    if (!SetFilePointerEx(value, zero, &position, FILE_CURRENT) || position.QuadPart < 0) {
        return {};
    }
    return {value, handle->generation, session, static_cast<std::uint64_t>(position.QuadPart)};
}

void end_database_read(const DatabaseIoReadToken& token, BOOL success, DWORD bytes,
                       std::uintptr_t caller) noexcept {
    LastError preserved;
    if (!success || !bytes || token.handle == INVALID_HANDLE_VALUE) {
        return;
    }
    Lock locked;
    const auto* handle = find(token.handle);
    if (!handle || !handle->current || handle->generation != token.generation ||
        session != token.session) {
        return;
    }
    LARGE_INTEGER zero{}, position{};
    // Exclude reads whose observed file position changed concurrently.
    if (!SetFilePointerEx(token.handle, zero, &position, FILE_CURRENT) || position.QuadPart < 0 ||
        static_cast<std::uint64_t>(position.QuadPart) != token.offset + bytes) {
        return;
    }
    const bool rva = caller >= executable_base && caller < executable_end;
    ++total_reads;
    total_bytes += bytes;
    reads[(total_reads - 1) % reads.size()] = {
        token.offset, bytes, rva ? caller - executable_base : caller, total_reads, rva};
}

std::uint64_t begin_database_close(HANDLE value) noexcept {
    LastError preserved;
    Lock locked;
    const auto* handle = find(value);
    return handle ? handle->generation : 0;
}

void observe_database_close(HANDLE value, BOOL success, std::uint64_t expected) noexcept {
    LastError preserved;
    if (!success) {
        return;
    }
    Lock locked;
    if (auto* handle = find(value); handle && handle->generation == expected) {
        *handle = {};
    }
}

DatabaseIoSnapshot database_io_snapshot() {
    LastError preserved;
    Lock locked;
    DatabaseIoSnapshot result;
    result.path = path.data();
    result.session = session;
    for (const auto& handle : handles) {
        result.open |= handle.current;
    }
    result.total_reads = total_reads;
    result.total_bytes = total_bytes;
    const auto count = (std::min)(total_reads, static_cast<std::uint64_t>(reads.size()));
    result.reads.reserve(static_cast<std::size_t>(count));
    for (auto sequence = total_reads - count; sequence < total_reads; ++sequence) {
        result.reads.push_back(reads[sequence % reads.size()]);
    }
    return result;
}

}
