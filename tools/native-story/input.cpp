#include "identity.h"
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
bool same_path(const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto first = std::filesystem::canonical(left).wstring();
    const auto second = std::filesystem::canonical(right).wstring();
    return CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) == CSTR_EQUAL;
}

struct Process {
    HANDLE handle = nullptr;

    ~Process() {
        if (handle) {
            CloseHandle(handle);
        }
    }
};

struct Target {
    DWORD pid;
    HWND window = nullptr;
    std::filesystem::path expected;
};

BOOL CALLBACK find_window(HWND window, LPARAM context) {
    auto& target = *reinterpret_cast<Target*>(context);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    wchar_t name[128]{};
    GetClassNameW(window, name, 128);
    if ((!target.pid || pid == target.pid) && !wcscmp(name, L"XFiles.exe") &&
        IsWindowVisible(window)) {
        if (target.expected.empty()) {
            target.window = window;
        } else {
            const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process) {
                wchar_t path[32768]{};
                DWORD size = static_cast<DWORD>(std::size(path));
                if (QueryFullProcessImageNameW(process, 0, path, &size) &&
                    same_path(path, target.expected)) {
                    target.window = window;
                }
                CloseHandle(process);
            }
        }
    }
    return TRUE;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

unsigned number(const wchar_t* text, unsigned maximum) {
    std::size_t end = 0;
    const auto value = std::stoul(text, &end);
    require(end == wcslen(text) && value <= maximum, "Invalid numeric argument.");
    return static_cast<unsigned>(value);
}

Process owned_process(DWORD pid, const std::filesystem::path& folder, bool terminate = false) {
    require(pid != 0, "A nonzero process ID is required.");
    const auto expected = std::filesystem::canonical(folder / L"XFiles.exe");
    require(identify(expected).edition != nullptr, "Unsupported original game executable.");
    const auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE |
                                        (terminate ? PROCESS_TERMINATE : 0),
                                    FALSE, pid);
    if (!handle) {
        require(GetLastError() == ERROR_INVALID_PARAMETER, "Cannot query the owned process.");
        return {};
    }
    if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) {
        CloseHandle(handle);
        return {};
    }
    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    const bool matches =
        QueryFullProcessImageNameW(handle, 0, path, &size) && same_path(path, expected);
    if (!matches) {
        CloseHandle(handle);
        throw std::runtime_error("The PID does not belong to the selected game path.");
    }
    return {handle};
}

std::vector<DWORD> processes(const std::filesystem::path& folder, bool launchers) {
    const auto game = std::filesystem::canonical(folder / L"XFiles.exe");
    require(identify(game).edition != nullptr, "Unsupported original game executable.");
    Process snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    require(snapshot.handle != INVALID_HANDLE_VALUE, "Cannot enumerate game processes.");
    std::vector<DWORD> result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    require(Process32FirstW(snapshot.handle, &entry), "Cannot read the process list.");
    do {
        const bool launcher = launchers && !_wcsicmp(entry.szExeFile, L"XFilesPlay.exe");
        if (_wcsicmp(entry.szExeFile, L"XFiles.exe") && !launcher) {
            continue;
        }
        Process process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
        require(process.handle != nullptr, "Cannot inspect an existing game process.");
        wchar_t path[32768]{};
        DWORD size = static_cast<DWORD>(std::size(path));
        require(QueryFullProcessImageNameW(process.handle, 0, path, &size),
                "Cannot identify an existing game process.");
        if (same_path(path, folder / (launcher ? L"XFilesPlay.exe" : L"XFiles.exe"))) {
            result.push_back(entry.th32ProcessID);
        }
    } while (Process32NextW(snapshot.handle, &entry));
    return result;
}

HWND owned_window(DWORD pid, const std::filesystem::path& folder) {
    const auto expected = std::filesystem::canonical(folder / L"XFiles.exe");
    require(identify(expected).edition != nullptr, "Unsupported original game executable.");
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    require(process != nullptr, "Cannot query the target process.");
    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    const bool found = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    DWORD code = 0;
    const bool active = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    require(found && active && same_path(path, expected),
            "The target PID does not belong to the selected game.");
    Target target{pid, nullptr, {}};
    EnumWindows(find_window, reinterpret_cast<LPARAM>(&target));
    require(target.window != nullptr, "No visible game window belongs to the target PID.");
    return target.window;
}

void foreground(HWND window) {
    if (GetForegroundWindow() == window) {
        return;
    }
    if (IsIconic(window)) {
        ShowWindowAsync(window, SW_RESTORE);
    }
    const auto foreground_thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const auto current_thread = GetCurrentThreadId();
    const bool attached = foreground_thread != current_thread &&
                          AttachThreadInput(current_thread, foreground_thread, TRUE);
    SetForegroundWindow(window);
    if (attached) {
        AttachThreadInput(current_thread, foreground_thread, FALSE);
    }
    for (unsigned attempt = 0; attempt < 20 && GetForegroundWindow() != window; ++attempt) {
        Sleep(25);
    }
    require(GetForegroundWindow() == window, "The owned test game must have focus.");
}

void key(unsigned value, const std::wstring& mode) {
    require(value && value <= 254, "Invalid virtual key.");
    require(mode == L"up" || mode == L"down" || mode == L"tap", "Unknown key mode.");
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(value);
    if (mode != L"up") {
        require(SendInput(1, &input, sizeof(input)) == 1, "Cannot press the test key.");
    }
    if (mode == L"tap") {
        Sleep(25);
    }
    if (mode != L"down") {
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        require(SendInput(1, &input, sizeof(input)) == 1, "Cannot release the test key.");
    }
}

void click(HWND window, unsigned x, unsigned y, bool right) {
    RECT bounds{};
    POINT point{};
    require(GetClientRect(window, &bounds) && bounds.right > 0 && bounds.bottom > 0,
            "Cannot read the game client bounds.");
    require(std::abs(bounds.right * 480 - bounds.bottom * 640) <= 640,
            "Native regression input requires a 4:3 client window.");
    point.x = MulDiv(static_cast<int>(x), bounds.right, 640);
    point.y = MulDiv(static_cast<int>(y), bounds.bottom, 480);
    require(ClientToScreen(window, &point) && SetCursorPos(point.x, point.y),
            "Cannot position the test pointer.");
    Sleep(25);
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    require(SendInput(1, &input, sizeof(input)) == 1, "Cannot press the test pointer.");
    Sleep(25);
    input.mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    require(SendInput(1, &input, sizeof(input)) == 1, "Cannot release the test pointer.");
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 &&
            (std::wstring(argv[1]) == L"--find" || std::wstring(argv[1]) == L"--idle")) {
            const bool idle = std::wstring(argv[1]) == L"--idle";
            const auto found = processes(argv[2], idle);
            if (idle) {
                require(found.empty(), "The selected game or launcher is already running.");
            } else if (found.empty()) {
                return 3;
            } else {
                require(found.size() == 1, "Multiple selected game processes are running.");
                std::cout << found.front() << '\n';
            }
            return 0;
        }
        if (argc == 4 &&
            (std::wstring(argv[1]) == L"--status" || std::wstring(argv[1]) == L"--close")) {
            const bool close = std::wstring(argv[1]) == L"--close";
            const auto pid = number(argv[2], MAXDWORD);
            const auto process = owned_process(pid, argv[3], close);
            if (!process.handle) {
                return 3;
            }
            if (close) {
                Target target{pid, nullptr, {}};
                EnumWindows(find_window, reinterpret_cast<LPARAM>(&target));
                if (target.window) {
                    DWORD_PTR ignored = 0;
                    SendMessageTimeoutW(target.window, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 1000,
                                        &ignored);
                }
                if (WaitForSingleObject(process.handle, 3000) == WAIT_TIMEOUT) {
                    require(TerminateProcess(process.handle, 1),
                            "Cannot stop the owned test game.");
                }
                require(WaitForSingleObject(process.handle, 5000) == WAIT_OBJECT_0,
                        "The owned game did not exit. Preserve files until it closes.");
            }
            return 0;
        }
        require(argc >= 5, "Usage: xfiles-native-input <pid> <game> <key|click> <arguments>");
        const auto window = owned_window(number(argv[1], MAXDWORD), argv[2]);
        foreground(window);
        const std::wstring command(argv[3]);
        if (command == L"key") {
            require(argc == 6, "key requires a virtual key and tap/down/up.");
            key(number(argv[4], 254), argv[5]);
        } else if (command == L"click") {
            require(argc == 6 || argc == 7, "click requires logical x/y and optional right.");
            require(argc != 7 || std::wstring(argv[6]) == L"right", "Unknown mouse button.");
            click(window, number(argv[4], 639), number(argv[5], 479), argc == 7);
        } else {
            throw std::runtime_error("Unknown input command.");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
