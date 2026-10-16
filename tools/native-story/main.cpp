#include "state.h"
#include "identity.h"
#include <tlhelp32.h>
#include <array>
#include <cerrno>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
class Handle {
public:
    explicit Handle(HANDLE value) : value(value) {
        if (!value || value == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("cannot_open_process_or_modules");
        }
    }

    ~Handle() {
        CloseHandle(value);
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE value;
};

bool same_path(const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto a = std::filesystem::canonical(left).native();
    const auto b = std::filesystem::canonical(right).native();
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

template <class T> T read(HANDLE process, std::uintptr_t address) {
    T value{};
    SIZE_T copied = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<const void*>(address), &value, sizeof(value),
                           &copied) ||
        copied != sizeof(value)) {
        throw std::runtime_error("cannot_read_remote_image_header");
    }
    return value;
}

std::uintptr_t image_base(DWORD pid, HANDLE process, const std::filesystem::path& expected) {
    const Handle modules(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (!Module32FirstW(modules.value, &module) || !same_path(module.szExePath, expected)) {
        throw std::runtime_error("main_module_path_mismatch");
    }
    const auto base = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
    const auto dos = read<IMAGE_DOS_HEADER>(process, base);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos) ||
        dos.e_lfanew > 1024 * 1024) {
        throw std::runtime_error("invalid_remote_dos_header");
    }
    const auto nt = read<IMAGE_NT_HEADERS32>(process, base + dos.e_lfanew);
    std::ifstream source(expected, std::ios::binary);
    source.seekg(dos.e_lfanew);
    IMAGE_NT_HEADERS32 disk{};
    if (!source.read(reinterpret_cast<char*>(&disk), sizeof(disk)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.FileHeader.TimeDateStamp != disk.FileHeader.TimeDateStamp ||
        nt.FileHeader.NumberOfSections != disk.FileHeader.NumberOfSections ||
        nt.OptionalHeader.SizeOfImage != disk.OptionalHeader.SizeOfImage ||
        nt.OptionalHeader.SizeOfImage != module.modBaseSize ||
        nt.OptionalHeader.AddressOfEntryPoint != disk.OptionalHeader.AddressOfEntryPoint) {
        throw std::runtime_error("remote_image_identity_mismatch");
    }
    return base;
}

struct Window {
    DWORD pid;
    HWND hwnd = nullptr;
};

BOOL CALLBACK find_window(HWND hwnd, LPARAM parameter) {
    auto& result = *reinterpret_cast<Window*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == result.pid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
        result.hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error("usage: xfiles-native-state.exe <pid> <game-directory>");
        }
        wchar_t* end = nullptr;
        errno = 0;
        const auto parsed = std::wcstoul(argv[1], &end, 10);
        if (errno || end == argv[1] || *end || !parsed) {
            throw std::runtime_error("invalid_pid");
        }
        const auto pid = static_cast<DWORD>(parsed);
        const auto expected =
            std::filesystem::canonical(std::filesystem::path(argv[2]) / L"XFiles.exe");
        const Handle process(
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid));
        std::vector<wchar_t> path(32768);
        DWORD size = static_cast<DWORD>(path.size());
        if (!QueryFullProcessImageNameW(process.value, 0, path.data(), &size) ||
            !same_path(std::filesystem::path(std::wstring(path.data(), size)), expected)) {
            throw std::runtime_error("process_path_mismatch");
        }
        const auto identity = identify(expected);
        if (!identity.build || !identity.build->profile) {
            throw std::runtime_error("unsupported_executable_sha256");
        }
        const auto image = image_base(pid, process.value, expected);
        Window window{pid};
        EnumWindows(find_window, reinterpret_cast<LPARAM>(&window));
        const auto state = native_story::snapshot(process.value, image, *identity.build);
        std::cout << "{\"pid\":" << pid
                  << ",\"hwnd\":" << reinterpret_cast<std::uintptr_t>(window.hwnd)
                  << ",\"sha256\":" << native_story::quote(identity.sha256)
                  << ",\"snapshot\":" << state << "}\n";
        return state.find("\"status\":\"ok\"") == std::string::npos ? 2 : 0;
    } catch (const std::exception& error) {
        std::cout << "{\"status\":\"unavailable\",\"reason\":" << native_story::quote(error.what())
                  << "}\n";
        return 2;
    }
}
