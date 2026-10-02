#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace game_assets {

struct DatabaseIoRead {
    std::uint64_t offset = 0;
    std::uint32_t size = 0;
    std::uintptr_t caller = 0;
    std::uint64_t sequence = 0;
    bool caller_is_rva = false;
};

struct DatabaseIoSnapshot {
    std::wstring path;
    bool open = false;
    std::uint64_t session = 0;
    std::uint64_t total_reads = 0;
    std::uint64_t total_bytes = 0;
    std::vector<DatabaseIoRead> reads;
};

struct DatabaseIoReadToken {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::uint64_t generation = 0;
    std::uint64_t session = 0;
    std::uint64_t offset = 0;
};

DatabaseIoSnapshot database_io_snapshot();
void database_io_executable(HMODULE executable) noexcept;
void observe_database_open(HANDLE handle, const char* requested, DWORD flags) noexcept;
DatabaseIoReadToken begin_database_read(HANDLE handle, OVERLAPPED* overlapped) noexcept;
void end_database_read(const DatabaseIoReadToken& token, BOOL success, DWORD bytes,
                       std::uintptr_t caller) noexcept;
std::uint64_t begin_database_close(HANDLE handle) noexcept;
void observe_database_close(HANDLE handle, BOOL success, std::uint64_t generation) noexcept;

}
