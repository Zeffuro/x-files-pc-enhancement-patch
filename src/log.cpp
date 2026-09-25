#include "runtime.h"
#include "diagnostics/log_file.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_set>
#include <vector>
#include <windows.h>

namespace {

SRWLOCK lock = SRWLOCK_INIT;

void write_line(const char* state, std::uint32_t selector, const char* name, std::uintptr_t caller,
                std::uintptr_t module_base = 0) {
    std::vector<wchar_t> path(32768);
    const DWORD length =
        GetEnvironmentVariableW(L"XFILES_PATCH_LOG", path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= std::size(path)) {
        return;
    }
    char line[512];
    const auto base =
        module_base ? module_base : reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    SYSTEMTIME time{};
    GetSystemTime(&time);
    const int count =
        sprintf_s(line,
                  "%04u-%02u-%02uT%02u:%02u:%02uZ %s selector=0x%08X name=%.200s "
                  "caller=0x%08X rva=0x%08X thread=%lu\r\n",
                  time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, state,
                  selector, name, caller, caller ? caller - base : 0, GetCurrentThreadId());
    if (count <= 0) {
        return;
    }
    const std::filesystem::path filename(path.data());
    AcquireSRWLockExclusive(&lock);
    diagnostics::append_log(filename, std::string_view(line, count));
    ReleaseSRWLockExclusive(&lock);
}
}

void trace_call(std::uint32_t selector, const char* name, std::uintptr_t caller) {
    static const bool verbose = GetEnvironmentVariableW(L"XFILES_PATCH_TRACE_ALL", nullptr, 0) != 0;
    thread_local std::unordered_set<std::uint64_t> seen;
    const auto key = (static_cast<std::uint64_t>(selector) << 32) | caller;
    if (!verbose && !seen.insert(key).second) {
        return;
    }
    write_line("CALL", selector, name, caller);
}

void trace_value(const char* name, std::uint32_t value) {
    write_line("VALUE", value, name, 0);
}

[[noreturn]] void unsupported(std::uint32_t selector, const char* name, std::uintptr_t caller) {
    write_line("UNSUPPORTED", selector, name, caller);
    void* frames[24]{};
    const auto count = CaptureStackBackTrace(0, 24, frames, nullptr);
    for (USHORT index = 0; index < count; ++index) {
        HMODULE module = nullptr;
        char path[MAX_PATH]{};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[index]), &module) &&
            GetModuleFileNameA(module, path, MAX_PATH)) {
            const char* basename = strrchr(path, '\\');
            write_line("STACK",
                       static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(frames[index]) -
                                                  reinterpret_cast<std::uintptr_t>(module)),
                       basename ? basename + 1 : path,
                       reinterpret_cast<std::uintptr_t>(frames[index]),
                       reinterpret_cast<std::uintptr_t>(module));
        }
    }
    ExitProcess(unsupported_exit);
}
