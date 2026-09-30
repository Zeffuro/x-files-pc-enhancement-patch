#include "enhancements/ui/controller_dialog.h"
#include "enhancements/ui/controller_resources.h"
#include "enhancements/controller_state.h"
#include "localization/ui.h"
#include "settings.h"

#include <array>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::array<enhancements::input::RawState, enhancements::input::max_devices> samples{};
std::array<bool, enhancements::input::max_devices> connected{};
constexpr std::array action_names{
    L"Use / confirm", L"Inventory / focus", L"Examine",     L"Back",
    L"Skip movie",    L"Menu / resume",     L"Previous",    L"Next",
    L"Evidence",      L"Fast-forward",      L"Equip / aim", L"Hotspot selection"};
std::function<void(HWND)> scenario;
std::exception_ptr dialog_error;
Settings* outer = nullptr;
controller::Profile initialized_profile;
unsigned initialized = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

unsigned binding(HWND window) {
    return static_cast<unsigned>(
        SendDlgItemMessageW(window, IDC_CONTROLLER_BINDING, CB_GETCURSEL, 0, 0));
}

void select_action(HWND window, controller::Action value) {
    SendDlgItemMessageW(window, IDC_CONTROLLER_ACTIONS, LB_SETCURSEL, static_cast<WPARAM>(value),
                        0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(IDC_CONTROLLER_ACTIONS, LBN_SELCHANGE), 0);
}

void assign(HWND window, controller::Action action, controller::Binding value) {
    select_action(window, action);
    SendDlgItemMessageW(window, IDC_CONTROLLER_BINDING, CB_SETCURSEL, static_cast<WPARAM>(value),
                        0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(IDC_CONTROLLER_BINDING, CBN_SELCHANGE), 0);
}

void tick(HWND window) {
    SendMessageW(window, WM_TIMER, 1, 0);
}

void capture(HWND window) {
    SendMessageW(window, WM_COMMAND, IDC_CONTROLLER_CAPTURE, 0);
}

void finish(HWND window, bool accept) {
    SendMessageW(window, WM_COMMAND, accept ? IDOK : IDCANCEL, 0);
}

void verify_initial(HWND window) {
    require(!IsWindowVisible(window), "Dialog was displayed before the hidden test hook.");
    require(SendDlgItemMessageW(window, IDC_CONTROLLER_ACTIONS, LB_GETCOUNT, 0, 0) ==
                controller::action_count,
            "Dialog does not expose every logical action.");
    require(SendDlgItemMessageW(window, IDC_CONTROLLER_BINDING, CB_GETCOUNT, 0, 0) ==
                controller::binding_count,
            "Dialog does not expose every supported binding.");
    require(GetDlgItemInt(window, IDC_CONTROLLER_DEADZONE, nullptr, FALSE) ==
                    initialized_profile.deadzone &&
                GetDlgItemInt(window, IDC_CONTROLLER_SENSITIVITY, nullptr, FALSE) ==
                    initialized_profile.sensitivity &&
                GetDlgItemInt(window, IDC_CONTROLLER_TRIGGER, nullptr, FALSE) ==
                    initialized_profile.trigger_threshold,
            "Opening the dialog lost draft calibration.");
    require(SendDlgItemMessageW(window, IDC_CONTROLLER_CURVE, CB_GETCURSEL, 0, 0) ==
                static_cast<LRESULT>(initialized_profile.curve),
            "Opening the dialog lost draft response curve.");
    require((IsDlgButtonChecked(window, IDC_CONTROLLER_INVERT_Y) == BST_CHECKED) ==
                initialized_profile.invert_y,
            "Opening the dialog lost draft inversion.");
    for (unsigned index = 0; index < controller::action_count; ++index) {
        select_action(window, static_cast<controller::Action>(index));
        require(binding(window) == static_cast<unsigned>(initialized_profile.bindings[index]),
                "Opening the dialog lost a draft assignment.");
        wchar_t label[256]{};
        SendDlgItemMessageW(window, IDC_CONTROLLER_ACTIONS, LB_GETTEXT, index,
                            reinterpret_cast<LPARAM>(label));
        const auto expected = std::wstring(ui::translate(action_names[index])) + L"  :  " +
                              ui::translate(controller::binding_names[binding(window)]);
        require(label == expected, "An action row did not show its player-facing label.");
    }
    WINDOWINFO bounds{sizeof(WINDOWINFO)};
    require(GetWindowInfo(window, &bounds) && bounds.rcWindow.bottom - bounds.rcWindow.top < 720,
            "Controller dialog does not fit the laptop height at the test DPI.");
}

LRESULT CALLBACK initialized_dialog(int code, WPARAM parameter, LPARAM data) {
    const auto* message = reinterpret_cast<CWPRETSTRUCT*>(data);
    if (code >= 0 && message->message == WM_INITDIALOG &&
        GetDlgItem(message->hwnd, IDC_CONTROLLER_ACTIONS)) {
        ++initialized;
        try {
            verify_initial(message->hwnd);
            scenario(message->hwnd);
        } catch (...) {
            dialog_error = std::current_exception();
            finish(message->hwnd, false);
        }
    }
    return CallNextHookEx(nullptr, code, parameter, data);
}

void show(Settings& draft, std::function<void(HWND)> run) {
    outer = &draft;
    initialized_profile = draft.controller_profile;
    scenario = std::move(run);
    dialog_error = nullptr;
    samples = {};
    connected = {};
    const auto before = initialized;
    const auto hook =
        SetWindowsHookExW(WH_CALLWNDPROCRET, initialized_dialog, nullptr, GetCurrentThreadId());
    require(hook != nullptr, "Cannot intercept dialog initialization.");
    try {
        enhancements::show_controller_dialog(nullptr, GetModuleHandleW(nullptr), draft);
    } catch (...) {
        UnhookWindowsHookEx(hook);
        throw;
    }
    UnhookWindowsHookEx(hook);
    if (dialog_error) {
        std::rethrow_exception(dialog_error);
    }
    require(initialized == before + 1, "Dialog did not initialize exactly once.");
}

void transactions() {
    Settings draft;
    draft.gamepad = false;
    draft.vibration = false;
    draft.controller_profile.deadzone = 12000;
    draft.controller_profile.sensitivity = 165;
    draft.controller_profile.curve = controller::Curve::Cubic;
    draft.controller_profile.invert_y = true;
    draft.controller_profile.trigger_threshold = 75;
    controller::bind(draft.controller_profile, controller::Action::Activate,
                     controller::Binding::RightShoulder);
    const auto initial = draft.controller_profile;
    show(draft, [&](HWND window) {
        assign(window, controller::Action::Menu, controller::Binding::A);
        SetDlgItemInt(window, IDC_CONTROLLER_SENSITIVITY, 300, FALSE);
        require(outer->controller_profile == initial, "Editing published the nested draft.");
        finish(window, false);
    });
    require(draft.controller_profile == initial, "Nested Cancel changed the outer draft.");
    show(draft, [&](HWND window) {
        assign(window, controller::Action::Menu, controller::Binding::A);
        SetDlgItemInt(window, IDC_CONTROLLER_SENSITIVITY, 300, FALSE);
        SetDlgItemInt(window, IDC_CONTROLLER_DEADZONE, 0, FALSE);
        SetDlgItemInt(window, IDC_CONTROLLER_TRIGGER, 254, FALSE);
        CheckDlgButton(window, IDC_CONTROLLER_INVERT_X, BST_CHECKED);
        finish(window, true);
    });
    auto expected = initial;
    controller::bind(expected, controller::Action::Menu, controller::Binding::A);
    expected.sensitivity = 300;
    expected.deadzone = 0;
    expected.trigger_threshold = 254;
    expected.invert_x = true;
    require(draft.controller_profile == expected && !draft.gamepad && !draft.vibration,
            "Nested OK lost calibration, swap assignments or independent settings.");
    show(draft, [&](HWND window) {
        SendMessageW(window, WM_COMMAND, IDC_CONTROLLER_RESET, 0);
        require(outer->controller_profile == expected, "Reset published into the outer draft.");
        require(GetDlgItemInt(window, IDC_CONTROLLER_DEADZONE, nullptr, FALSE) == 7849,
                "Reset did not restore the recommended deadzone.");
        finish(window, false);
    });
    require(draft.controller_profile == expected, "Reset followed by Cancel lost the draft.");
    show(draft, [](HWND window) {
        SendMessageW(window, WM_COMMAND, IDC_CONTROLLER_RESET, 0);
        finish(window, true);
    });
    require(draft.controller_profile == controller::Profile{}, "Accepted Reset lost defaults.");
}

void validation() {
    constexpr std::array cases{
        std::pair{IDC_CONTROLLER_DEADZONE, L"30001"},
        std::pair{IDC_CONTROLLER_SENSITIVITY, L"24"},
        std::pair{IDC_CONTROLLER_SENSITIVITY, L"301"},
        std::pair{IDC_CONTROLLER_TRIGGER, L"255"},
        std::pair{IDC_CONTROLLER_TRIGGER, L""},
        std::pair{IDC_CONTROLLER_DEADZONE, L"999999999999999999999999"},
        std::pair{IDC_CONTROLLER_DEADZONE, L"0000000000000000000000000000000030001"},
        std::pair{IDC_CONTROLLER_TRIGGER, L"0000000000000000000000000000000000255"},
        std::pair{IDC_CONTROLLER_DEADZONE, L"-1"}};
    Settings draft;
    for (const auto& [control, value] : cases) {
        draft.controller_profile = {};
        show(draft, [&](HWND window) {
            SetDlgItemTextW(window, control, value);
            finish(window, true);
            require(outer->controller_profile == initialized_profile,
                    "Invalid calibration published an outer draft.");
            require(GetFocus() == GetDlgItem(window, control),
                    "Invalid calibration did not focus the relevant field.");
            SetDlgItemInt(window, control, control == IDC_CONTROLLER_SENSITIVITY ? 25 : 0, FALSE);
            finish(window, true);
        });
    }
    show(draft, [&](HWND window) {
        SendDlgItemMessageW(window, IDC_CONTROLLER_CURVE, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        finish(window, true);
        require(GetFocus() == GetDlgItem(window, IDC_CONTROLLER_CURVE),
                "Missing response curve did not focus its combo.");
        finish(window, false);
    });
}

void captures() {
    Settings draft;
    show(draft, [&](HWND window) {
        select_action(window, controller::Action::Menu);
        connected[0] = true;
        samples[0].buttons = 0x1000;
        capture(window);
        tick(window);
        require(binding(window) == static_cast<unsigned>(controller::Binding::Start),
                "Capture accepted an already held button.");
        connected[1] = true;
        samples[1].buttons = 0x2000;
        tick(window);
        require(!IsWindowEnabled(GetDlgItem(window, IDC_CONTROLLER_ACTIONS)),
                "Capture changed controller while its first controller was held.");
        samples[0] = {};
        tick(window);
        samples[0].buttons = 0x3000;
        tick(window);
        require(!IsWindowEnabled(GetDlgItem(window, IDC_CONTROLLER_ACTIONS)),
                "Capture accepted a chord.");
        samples[0].buttons = 0x2000;
        tick(window);
        require(binding(window) == static_cast<unsigned>(controller::Binding::Start),
                "Capture did not require neutral after a chord.");
        samples[0] = {};
        tick(window);
        samples[0].left_trigger = 150;
        tick(window);
        require(binding(window) == static_cast<unsigned>(controller::Binding::LeftTrigger) &&
                    IsWindowEnabled(GetDlgItem(window, IDC_CONTROLLER_ACTIONS)),
                "Neutral trigger capture did not assign and finish.");
        select_action(window, controller::Action::Aim);
        require(binding(window) == static_cast<unsigned>(controller::Binding::Start),
                "Captured trigger did not swap the displaced action.");
        require(outer->controller_profile == initialized_profile,
                "Capture published before nested OK.");
        finish(window, true);
    });
    require(draft.controller_profile.bindings[static_cast<unsigned>(controller::Action::Menu)] ==
                controller::Binding::LeftTrigger,
            "Accepted capture was lost.");
    const auto accepted = draft.controller_profile;
    show(draft, [&](HWND window) {
        select_action(window, controller::Action::Activate);
        connected[0] = true;
        capture(window);
        tick(window);
        samples[0].buttons = 0x0001;
        tick(window);
        require(!IsWindowEnabled(GetDlgItem(window, IDC_CONTROLLER_BINDING)),
                "Fixed D-pad was captured as a remappable binding.");
        connected[0] = false;
        tick(window);
        require(IsWindowEnabled(GetDlgItem(window, IDC_CONTROLLER_BINDING)),
                "Disconnect did not cancel capture.");
        connected[0] = true;
        samples[0].buttons = 0x8000;
        tick(window);
        require(binding(window) == static_cast<unsigned>(controller::Binding::A),
                "Reconnect rebound an action without capture.");
        capture(window);
        tick(window);
        samples[0] = {};
        tick(window);
        SendMessageW(window, WM_ACTIVATE, WA_INACTIVE, 0);
        samples[0].buttons = 0x4000;
        tick(window);
        require(IsWindowEnabled(GetDlgItem(window, IDC_CONTROLLER_BINDING)) &&
                    binding(window) == static_cast<unsigned>(controller::Binding::A),
                "Losing focus did not cancel capture.");
        finish(window, false);
    });
    require(draft.controller_profile == accepted, "Cancelled capture changed accepted draft.");
}

void localization() {
    constexpr std::array labels{L"Configure controller",
                                L"Configure controller...",
                                L"Bindings",
                                L"Choose an action, then assign a button.",
                                L"Assigned button",
                                L"Capture button",
                                L"Cancel capture",
                                L"Calibration",
                                L"Stick deadzone (0-30000)",
                                L"Sensitivity (25-300%)",
                                L"Response curve",
                                L"Trigger threshold (0-254)",
                                L"Invert stick X",
                                L"Invert stick Y",
                                L"Live input",
                                L"Reset recommended",
                                L"No controller connected.",
                                L"Press one button or trigger to assign it.",
                                L"Release all controls, then press one button.",
                                L"Enter a value in the range shown."};
    for (unsigned language = 1; language <= 5; ++language) {
        for (const auto* label : labels) {
            require(std::wstring(ui::translate(label, static_cast<ui::Language>(language))) !=
                        label,
                    "A controller dialog label is missing a localization.");
        }
        for (const auto* label : action_names) {
            require(std::wstring(ui::translate(label, static_cast<ui::Language>(language))) !=
                        label,
                    "A controller action is missing a localization.");
        }
    }
}

void calibrated_capture() {
    Settings draft;
    draft.controller_profile.deadzone = 12000;
    draft.controller_profile.trigger_threshold = 10;
    show(draft, [](HWND window) {
        select_action(window, controller::Action::Activate);
        connected[0] = true;
        samples[0].left_x = 10000;
        samples[0].left_trigger = 11;
        capture(window);
        tick(window);
        require(binding(window) == static_cast<unsigned>(controller::Binding::A),
                "Low-threshold held trigger was captured before neutral.");
        samples[0].left_trigger = 10;
        tick(window);
        samples[0].left_trigger = 11;
        tick(window);
        require(binding(window) == static_cast<unsigned>(controller::Binding::LeftTrigger) &&
                    IsWindowEnabled(GetDlgItem(window, IDC_CONTROLLER_BINDING)),
                "Capture ignored the configured drift deadzone or low trigger threshold.");
        finish(window, true);
    });
    draft.controller_profile.deadzone = 0;
    draft.controller_profile.trigger_threshold = 200;
    show(draft, [](HWND window) {
        select_action(window, controller::Action::Menu);
        connected[0] = true;
        samples[0].left_x = 1;
        capture(window);
        tick(window);
        samples[0].right_trigger = 201;
        tick(window);
        require(binding(window) == static_cast<unsigned>(controller::Binding::Start),
                "Capture ignored the configured zero stick deadzone.");
        samples[0].left_x = 0;
        samples[0].right_trigger = 200;
        tick(window);
        tick(window);
        require(binding(window) == static_cast<unsigned>(controller::Binding::Start),
                "Trigger threshold boundary was captured as a press.");
        samples[0].right_trigger = 201;
        tick(window);
        require(binding(window) == static_cast<unsigned>(controller::Binding::RightTrigger),
                "Capture ignored the configured high trigger threshold.");
        finish(window, true);
    });
}

}

namespace enhancements::input {
bool read_raw(unsigned player, RawState& state) {
    if (player >= max_devices || !connected[player]) {
        return false;
    }
    state = samples[player];
    return true;
}
}

int main() {
    try {
        const auto published = settings().controller_profile;
        transactions();
        validation();
        captures();
        calibrated_capture();
        localization();
        require(settings().controller_profile == published,
                "Nested dialog published runtime settings before the outer OK.");
        std::cout
            << "Controller dialog draft, calibration, neutral capture and localization passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
