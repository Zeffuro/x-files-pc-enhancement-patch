#include "enhancements/ui/quick_menu_dialog.h"
#include "enhancements/ui/resources.h"
#include "settings.h"

#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr std::array controls{IDC_QUICK_MENU_SAVE, IDC_QUICK_MENU_LOAD, IDC_QUICK_MENU_TRANSCRIPT,
                              IDC_QUICK_MENU_TWEAKS, IDC_QUICK_MENU_MENU};
std::exception_ptr dialog_error;
Settings expected, changes;
bool accept = false;
unsigned initialized = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

LRESULT CALLBACK initialized_dialog(int code, WPARAM parameter, LPARAM data) {
    const auto* message = reinterpret_cast<CWPRETSTRUCT*>(data);
    if (code >= 0 && message->message == WM_INITDIALOG &&
        GetDlgItem(message->hwnd, IDC_QUICK_MENU_ENABLE)) {
        const auto window = message->hwnd;
        ++initialized;
        try {
            require(!IsWindowVisible(window), "Dialog became visible before the test hook.");
            require((IsDlgButtonChecked(window, IDC_QUICK_MENU_ENABLE) == BST_CHECKED) ==
                        expected.quick_menu,
                    "Nested dialog lost the draft master setting.");
            for (std::size_t index = 0; index < controls.size(); ++index) {
                require((IsDlgButtonChecked(window, controls[index]) == BST_CHECKED) ==
                            expected.quick_menu_items[index],
                        "Nested dialog lost a draft button setting.");
                require((IsWindowEnabled(GetDlgItem(window, controls[index])) != FALSE) ==
                            expected.quick_menu,
                        "Master setting did not gate item controls.");
                CheckDlgButton(window, controls[index],
                               changes.quick_menu_items[index] ? BST_CHECKED : BST_UNCHECKED);
            }
            CheckDlgButton(window, IDC_QUICK_MENU_ENABLE,
                           changes.quick_menu ? BST_CHECKED : BST_UNCHECKED);
            SendMessageW(window, WM_COMMAND, IDC_QUICK_MENU_ENABLE, 0);
            for (const auto control : controls) {
                require((IsWindowEnabled(GetDlgItem(window, control)) != FALSE) ==
                            changes.quick_menu,
                        "Changing Enable did not update item controls.");
            }
        } catch (...) {
            dialog_error = std::current_exception();
        }
        SendMessageW(window, WM_COMMAND, accept && !dialog_error ? IDOK : IDCANCEL, 0);
    }
    return CallNextHookEx(nullptr, code, parameter, data);
}

void show(Settings& draft, const Settings& next, bool confirm) {
    expected = draft;
    changes = next;
    accept = confirm;
    dialog_error = nullptr;
    const auto before = initialized;
    const auto hook =
        SetWindowsHookExW(WH_CALLWNDPROCRET, initialized_dialog, nullptr, GetCurrentThreadId());
    require(hook != nullptr, "Cannot intercept the hidden dialog initialization.");
    try {
        enhancements::show_quick_menu_dialog(nullptr, GetModuleHandleW(nullptr), draft);
    } catch (...) {
        UnhookWindowsHookEx(hook);
        throw;
    }
    UnhookWindowsHookEx(hook);
    if (dialog_error) {
        std::rethrow_exception(dialog_error);
    }
    require(initialized == before + 1, "Nested dialog was not initialized exactly once.");
}

void verify_dialog() {
    const auto published = settings();
    Settings original;
    original.save_browser = false;
    original.dialogue_transcript = false;
    Settings draft = original;
    Settings next = original;
    next.quick_menu_items = {false, true, false, true, false};
    show(draft, next, false);
    require(draft.quick_menu && draft.quick_menu_items == original.quick_menu_items,
            "Nested Cancel changed the outer draft.");
    show(draft, next, true);
    require(draft.quick_menu && draft.quick_menu_items == next.quick_menu_items &&
                !draft.save_browser && !draft.dialogue_transcript,
            "Nested OK lost choices or changed independent enhancements.");
    next.quick_menu = false;
    show(draft, next, true);
    require(!draft.quick_menu && draft.quick_menu_items == next.quick_menu_items,
            "Disabling the menu discarded its button choices.");
    next.quick_menu = true;
    next.quick_menu_items.fill(false);
    show(draft, next, false);
    require(!draft.quick_menu && draft.quick_menu_items != next.quick_menu_items,
            "Cancel on reopening discarded the previously accepted draft.");
    show(draft, next, true);
    require(draft.quick_menu && draft.quick_menu_items == next.quick_menu_items,
            "The all-hidden selection was not accepted.");
    require(settings().quick_menu == published.quick_menu &&
                settings().quick_menu_items == published.quick_menu_items,
            "Nested OK published settings before outer OK.");
}

std::filesystem::path executable_path() {
    std::wstring value(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    require(size && size < value.size(), "Cannot locate the test executable.");
    value.resize(size);
    return value;
}

void verify_roundtrip() {
    const auto path = executable_path().parent_path() / L"patch.ini";
    for (unsigned mask = 0; mask < 32; ++mask) {
        Settings value;
        value.quick_menu = mask % 2 != 0;
        value.save_browser = true;
        value.dialogue_transcript = false;
        for (std::size_t index = 0; index < value.quick_menu_items.size(); ++index) {
            value.quick_menu_items[index] = (mask & (1U << index)) != 0;
        }
        save_settings(value);
        const auto loaded = read_settings(path);
        require(loaded.quick_menu == value.quick_menu &&
                    loaded.quick_menu_items == value.quick_menu_items && loaded.save_browser &&
                    !loaded.dialogue_transcript,
                "Quick menu settings did not roundtrip independently.");
    }
}

void isolated_roundtrip() {
    const auto root = std::filesystem::temp_directory_path() /
                      (L"xfiles-quick-menu-" + std::to_wstring(GetCurrentProcessId()));
    require(std::filesystem::create_directory(root), "Cannot create isolated settings folder.");

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{root};

    const auto child = root / L"quick-menu-dialog-test.exe";
    require(CopyFileW(executable_path().c_str(), child.c_str(), TRUE) != FALSE,
            "Cannot copy the isolated test executable.");
    std::wstring command = L"\"" + child.wstring() + L"\" --roundtrip";
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process{};
    require(CreateProcessW(child.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, root.c_str(), &startup, &process) != FALSE,
            "Cannot start isolated settings test.");
    const auto wait = WaitForSingleObject(process.hProcess, 30000);
    DWORD result = 1;
    GetExitCodeProcess(process.hProcess, &result);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    require(wait == WAIT_OBJECT_0 && result == 0, "Isolated settings roundtrip failed.");
}
}

int main(int argc, char**) {
    try {
        if (argc > 1) {
            verify_roundtrip();
        } else {
            verify_dialog();
            isolated_roundtrip();
        }
        std::cout << "Quick menu draft transactions and all 32 button settings passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
