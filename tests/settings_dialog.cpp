#include "enhancements/ui/settings_dialog.h"
#include "enhancements/ui/resources.h"
#include "enhancements/ui/controller_resources.h"
#include "enhancements/ui/tools_dialog.h"
#include "enhancements/ui/movie_preview.h"
#include "enhancements/edition.h"
#include "playback/movie.h"
#include "playback/output.h"
#include "settings.h"

#include <commctrl.h>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace playback {
std::vector<OutputDevice> output_devices() {
    return {{L"", L"Windows default"}};
}

std::vector<MovieHandle> pause_movies() {
    return {};
}

void resume_movies(const std::vector<MovieHandle>&) {}
}

namespace enhancements {
ToolsResult show_tools_dialog(HWND, HMODULE) {
    return {};
}

MovieContrast show_movie_preview(HWND, HMODULE, MovieContrast mode) {
    return mode;
}

void load_checkpoint(HWND, const std::filesystem::path&) {}

namespace game {
const Edition& edition() {
    return cd;
}
}
}

namespace devtools {
void request_inspector() {}
}

namespace {
std::exception_ptr dialog_error;
bool outer_accept = false;
bool nested_accept = false;
unsigned outer_count = 0, nested_count = 0;
controller::Profile expected, next;
bool preview = false;

void CALLBACK preview_close(HWND window, UINT, UINT_PTR timer, DWORD) {
    KillTimer(window, timer);
    SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
}

void CALLBACK preview_page(HWND window, UINT, UINT_PTR timer, DWORD) {
    static unsigned page = 0;
    if (page < 4) {
        const auto tab = GetDlgItem(window, IDC_SETTINGS_TABS);
        TabCtrl_SetCurSel(tab, page);
        NMHDR notification{tab, IDC_SETTINGS_TABS, TCN_SELCHANGE};
        SendMessageW(window, WM_NOTIFY, IDC_SETTINGS_TABS, reinterpret_cast<LPARAM>(&notification));
        std::cout << "Preview page " << page++ << std::endl;
    } else {
        KillTimer(window, timer);
        SendMessageW(window, WM_COMMAND, IDC_CONFIGURE_CONTROLLER, 0);
        SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
    }
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path executable_path() {
    std::wstring value(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    require(size && size < value.size(), "Cannot locate the test executable.");
    value.resize(size);
    return value;
}

std::string bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

LRESULT CALLBACK initialized_dialog(int code, WPARAM parameter, LPARAM data) {
    const auto* message = reinterpret_cast<CWPRETSTRUCT*>(data);
    if (code >= 0 && message->message == WM_INITDIALOG) {
        const auto window = message->hwnd;
        if (preview) {
            if (GetDlgItem(window, IDC_SETTINGS_TABS)) {
                SetTimer(window, 19, 4000, preview_page);
            } else if (GetDlgItem(window, IDC_CONTROLLER_ACTIONS)) {
                std::cout << "Preview controller" << std::endl;
                SetTimer(window, 19, 8000, preview_close);
            }
            return CallNextHookEx(nullptr, code, parameter, data);
        }
        if (GetDlgItem(window, IDC_SETTINGS_TABS)) {
            ++outer_count;
            try {
                require(TabCtrl_GetItemCount(GetDlgItem(window, IDC_SETTINGS_TABS)) == 4,
                        "Actual settings did not initialize four tabs.");
                CheckDlgButton(window, IDC_GAMEPAD, BST_UNCHECKED);
                SetDlgItemInt(window, IDC_CAPTION_SCALE, 142, FALSE);
                CheckDlgButton(window, IDC_SKIP_LOGIN, BST_CHECKED);
                SendMessageW(window, WM_COMMAND, IDC_CONFIGURE_CONTROLLER, 0);
                require(settings().controller_profile == expected,
                        "Nested OK published its profile before outer OK.");
            } catch (...) {
                dialog_error = std::current_exception();
            }
            SendMessageW(window, WM_COMMAND, outer_accept && !dialog_error ? IDOK : IDCANCEL, 0);
        } else if (GetDlgItem(window, IDC_CONTROLLER_ACTIONS)) {
            ++nested_count;
            try {
                require(GetDlgItemInt(window, IDC_CONTROLLER_DEADZONE, nullptr, FALSE) ==
                            expected.deadzone,
                        "Nested settings did not load the current profile.");
                SendDlgItemMessageW(window, IDC_CONTROLLER_ACTIONS, LB_SETCURSEL, 0, 0);
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(IDC_CONTROLLER_ACTIONS, LBN_SELCHANGE),
                             0);
                SendDlgItemMessageW(window, IDC_CONTROLLER_BINDING, CB_SETCURSEL,
                                    static_cast<WPARAM>(next.bindings[0]), 0);
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(IDC_CONTROLLER_BINDING, CBN_SELCHANGE),
                             0);
                SetDlgItemInt(window, IDC_CONTROLLER_DEADZONE, next.deadzone, FALSE);
                SetDlgItemInt(window, IDC_CONTROLLER_SENSITIVITY, next.sensitivity, FALSE);
                SetDlgItemInt(window, IDC_CONTROLLER_TRIGGER, next.trigger_threshold, FALSE);
                SendDlgItemMessageW(window, IDC_CONTROLLER_CURVE, CB_SETCURSEL,
                                    static_cast<WPARAM>(next.curve), 0);
                CheckDlgButton(window, IDC_CONTROLLER_INVERT_Y,
                               next.invert_y ? BST_CHECKED : BST_UNCHECKED);
            } catch (...) {
                dialog_error = std::current_exception();
            }
            SendMessageW(window, WM_COMMAND, nested_accept && !dialog_error ? IDOK : IDCANCEL, 0);
        }
    }
    return CallNextHookEx(nullptr, code, parameter, data);
}

void show(bool accept_outer, bool accept_nested, const controller::Profile& edited) {
    expected = settings().controller_profile;
    next = edited;
    outer_accept = accept_outer;
    nested_accept = accept_nested;
    dialog_error = nullptr;
    const auto outer_before = outer_count, nested_before = nested_count;
    const auto hook =
        SetWindowsHookExW(WH_CALLWNDPROCRET, initialized_dialog, nullptr, GetCurrentThreadId());
    require(hook != nullptr, "Cannot intercept the hidden production dialogs.");
    enhancements::show_settings_dialog(nullptr);
    UnhookWindowsHookEx(hook);
    if (dialog_error) {
        std::rethrow_exception(dialog_error);
    }
    require(outer_count == outer_before + 1 && nested_count == nested_before + 1,
            "Production outer or nested dialog was not initialized.");
}

void verify_transactions() {
    const auto path = executable_path().parent_path() / L"patch.ini";
    Settings original;
    original.controller_profile.deadzone = 9000;
    save_settings(original);
    const auto before = bytes(path);
    auto edited = original.controller_profile;
    controller::bind(edited, controller::Action::Activate, controller::Binding::X);
    edited.deadzone = 12000;
    edited.sensitivity = 175;
    edited.curve = controller::Curve::Linear;
    edited.trigger_threshold = 70;
    edited.invert_y = true;
    show(false, true, edited);
    require(bytes(path) == before && settings().controller_profile == original.controller_profile &&
                settings().gamepad && settings().caption_style.scale == 100,
            "Outer Cancel persisted nested OK or hidden-page edits.");
    show(true, true, edited);
    const auto persisted = read_settings(path);
    require(persisted.controller_profile == edited && !persisted.gamepad &&
                persisted.caption_style.scale == 142 && persisted.skip_workstation_login,
            "Outer OK lost controller or hidden-page settings.");
    auto discarded = edited;
    controller::bind(discarded, controller::Action::Activate, controller::Binding::B);
    discarded.deadzone = 18000;
    show(true, false, discarded);
    require(read_settings(path).controller_profile == edited &&
                settings().controller_profile == edited,
            "Nested Cancel changed the previously accepted controller profile.");
}

void isolated_test() {
    const auto root = std::filesystem::temp_directory_path() /
                      (L"xfiles-settings-dialog-" + std::to_wstring(GetCurrentProcessId()));
    require(std::filesystem::create_directory(root), "Cannot create isolated settings folder.");

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{root};

    const auto child = root / L"settings-dialog-test.exe";
    require(CopyFileW(executable_path().c_str(), child.c_str(), TRUE),
            "Cannot copy isolated test.");
    std::wstring command = L"\"" + child.wstring() + L"\" --transactions";
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process{};
    require(CreateProcessW(child.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, root.c_str(), &startup, &process),
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
    require(wait == WAIT_OBJECT_0 && result == 0,
            "Isolated production settings transactions failed.");
}
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--preview") {
            preview = true;
            const auto hook = SetWindowsHookExW(WH_CALLWNDPROCRET, initialized_dialog, nullptr,
                                                GetCurrentThreadId());
            require(hook != nullptr, "Cannot initialize preview hook.");
            enhancements::show_settings_dialog(nullptr);
            UnhookWindowsHookEx(hook);
        } else if (argc > 1) {
            verify_transactions();
        } else {
            isolated_test();
        }
        std::cout << "Actual settings nested and outer transactions passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
