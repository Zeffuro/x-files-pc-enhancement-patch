#include "controller_dialog.h"
#include "controller_resources.h"
#include "controller_profile.h"
#include "enhancements/controller_state.h"
#include "localization/ui.h"
#include "platform/tool_theme.h"
#include "settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <stdexcept>
#include <string>

namespace enhancements {
namespace {

constexpr UINT_PTR input_timer = 1;
constexpr std::array action_names{
    L"Use / confirm", L"Inventory / focus", L"Examine",     L"Back",
    L"Skip movie",    L"Menu / resume",     L"Previous",    L"Next",
    L"Evidence",      L"Fast-forward",      L"Equip / aim", L"Hotspot selection"};

struct Dialog {
    Settings& outer;
    controller::Profile profile;
    ui::Language language;
    bool capturing = false;
    bool neutral = false;
    bool active = true;
    unsigned player = input::no_device;
};

const wchar_t* text(const Dialog& dialog, const wchar_t* value) {
    return ui::translate(value, dialog.language);
}

void status(HWND window, const Dialog& dialog, const wchar_t* value) {
    SetDlgItemTextW(window, IDC_CONTROLLER_STATUS, text(dialog, value));
}

void center_dialog(HWND window) {
    WINDOWINFO owner_info{sizeof(WINDOWINFO)}, dialog_info{sizeof(WINDOWINFO)};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    const auto parent = GetParent(window);
    if (!GetWindowInfo(parent, &owner_info) || !GetWindowInfo(window, &dialog_info) ||
        !GetMonitorInfoW(MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST), &monitor)) {
        return;
    }
    const auto& owner = owner_info.rcWindow;
    const auto& bounds = dialog_info.rcWindow;
    const auto width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    const auto& area = monitor.rcWork;
    const auto x = std::clamp(owner.left + (owner.right - owner.left - width) / 2, area.left,
                              std::max(area.left, area.right - width));
    const auto y = std::clamp(owner.top + (owner.bottom - owner.top - height) / 2, area.top,
                              std::max(area.top, area.bottom - height));
    auto positions = BeginDeferWindowPos(1);
    if (positions) {
        positions = DeferWindowPos(positions, window, nullptr, x, y, 0, 0,
                                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (positions) {
            EndDeferWindowPos(positions);
        }
    }
}

unsigned action(HWND window) {
    const auto value = SendDlgItemMessageW(window, IDC_CONTROLLER_ACTIONS, LB_GETCURSEL, 0, 0);
    return value < 0 ? 0 : static_cast<unsigned>(value);
}

void select_binding(HWND window, const Dialog& dialog) {
    SendDlgItemMessageW(window, IDC_CONTROLLER_BINDING, CB_SETCURSEL,
                        static_cast<WPARAM>(dialog.profile.bindings[action(window)]), 0);
}

void refresh_bindings(HWND window, const Dialog& dialog) {
    const auto selected = action(window);
    SendDlgItemMessageW(window, IDC_CONTROLLER_ACTIONS, LB_RESETCONTENT, 0, 0);
    for (std::size_t index = 0; index < controller::action_count; ++index) {
        const auto label =
            std::wstring(text(dialog, action_names[index])) + L"  :  " +
            text(dialog,
                 controller::binding_names[static_cast<unsigned>(dialog.profile.bindings[index])]);
        SendDlgItemMessageW(window, IDC_CONTROLLER_ACTIONS, LB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendDlgItemMessageW(window, IDC_CONTROLLER_ACTIONS, LB_SETCURSEL, selected, 0);
    select_binding(window, dialog);
}

void stop_capture(HWND window, Dialog& dialog, const wchar_t* feedback) {
    dialog.capturing = false;
    dialog.neutral = false;
    dialog.player = input::no_device;
    EnableWindow(GetDlgItem(window, IDC_CONTROLLER_ACTIONS), TRUE);
    EnableWindow(GetDlgItem(window, IDC_CONTROLLER_BINDING), TRUE);
    SetDlgItemTextW(window, IDC_CONTROLLER_CAPTURE, text(dialog, L"Capture button"));
    status(window, dialog, feedback);
}

void populate(HWND window, const Dialog& dialog) {
    refresh_bindings(window, dialog);
    SetDlgItemInt(window, IDC_CONTROLLER_DEADZONE, dialog.profile.deadzone, FALSE);
    SetDlgItemInt(window, IDC_CONTROLLER_SENSITIVITY, dialog.profile.sensitivity, FALSE);
    SetDlgItemInt(window, IDC_CONTROLLER_TRIGGER, dialog.profile.trigger_threshold, FALSE);
    SendDlgItemMessageW(window, IDC_CONTROLLER_CURVE, CB_SETCURSEL,
                        static_cast<WPARAM>(dialog.profile.curve), 0);
    CheckDlgButton(window, IDC_CONTROLLER_INVERT_X,
                   dialog.profile.invert_x ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_CONTROLLER_INVERT_Y,
                   dialog.profile.invert_y ? BST_CHECKED : BST_UNCHECKED);
}

bool number(HWND window, int control, unsigned minimum, unsigned maximum, unsigned& value) {
    wchar_t buffer[32]{};
    if (GetWindowTextLengthW(GetDlgItem(window, control)) >= static_cast<int>(_countof(buffer))) {
        return false;
    }
    GetDlgItemTextW(window, control, buffer, _countof(buffer));
    if (!buffer[0]) {
        return false;
    }
    unsigned result = 0;
    for (const auto* character = buffer; *character; ++character) {
        if (*character < L'0' || *character > L'9' || result > maximum / 10) {
            return false;
        }
        result = result * 10 + static_cast<unsigned>(*character - L'0');
        if (result > maximum) {
            return false;
        }
    }
    if (result < minimum) {
        return false;
    }
    value = result;
    return true;
}

int calibration(HWND window, controller::Profile& profile) {
    if (!number(window, IDC_CONTROLLER_DEADZONE, 0, 30000, profile.deadzone)) {
        return IDC_CONTROLLER_DEADZONE;
    }
    if (!number(window, IDC_CONTROLLER_SENSITIVITY, 25, 300, profile.sensitivity)) {
        return IDC_CONTROLLER_SENSITIVITY;
    }
    if (!number(window, IDC_CONTROLLER_TRIGGER, 0, 254, profile.trigger_threshold)) {
        return IDC_CONTROLLER_TRIGGER;
    }
    const auto curve = SendDlgItemMessageW(window, IDC_CONTROLLER_CURVE, CB_GETCURSEL, 0, 0);
    if (curve < 0 || curve > static_cast<LRESULT>(controller::Curve::Cubic)) {
        return IDC_CONTROLLER_CURVE;
    }
    profile.curve = static_cast<controller::Curve>(curve);
    profile.invert_x = IsDlgButtonChecked(window, IDC_CONTROLLER_INVERT_X) == BST_CHECKED;
    profile.invert_y = IsDlgButtonChecked(window, IDC_CONTROLLER_INVERT_Y) == BST_CHECKED;
    return 0;
}

void poll_input(HWND window, Dialog& dialog) {
    input::RawState raw{};
    unsigned player = dialog.player;
    bool connected = false;
    if (player != input::no_device) {
        connected = input::read_raw(player, raw);
        if (!connected) {
            stop_capture(window, dialog, L"Controller disconnected. Capture cancelled.");
        }
    } else {
        for (player = 0; player < input::max_devices; ++player) {
            if (input::read_raw(player, raw)) {
                connected = true;
                break;
            }
        }
    }
    if (!connected) {
        SetDlgItemTextW(window, IDC_CONTROLLER_LIVE, text(dialog, L"No controller connected."));
        return;
    }
    auto preview = dialog.profile;
    if (calibration(window, preview)) {
        preview = dialog.profile;
    }
    std::wstring buttons;
    for (unsigned index = 0; index < controller::binding_count; ++index) {
        if (raw.buttons & controller::binding_masks[index]) {
            if (!buttons.empty()) {
                buttons += L" ";
            }
            buttons += text(dialog, controller::binding_names[index]);
        }
    }
    constexpr std::array directions{L" ↑", L" ↓", L" ←", L" →"};
    for (unsigned index = 0; index < directions.size(); ++index) {
        if (raw.buttons & (1U << index)) {
            buttons += directions[index];
        }
    }
    if (buttons.empty()) {
        buttons = L"-";
    }
    wchar_t values[256]{};
    std::swprintf(
        values, _countof(values),
        L"%ls %u    X: %d    Y: %d\nLT: %u    RT: %u    %ls: %ls\n%ls X: %.0f%%    Y: %.0f%%",
        text(dialog, L"Controller"), player + 1, raw.left_x, raw.left_y, raw.left_trigger,
        raw.right_trigger, text(dialog, L"Buttons"), buttons.c_str(), text(dialog, L"Calibrated"),
        controller::axis(raw.left_x, preview) * (preview.invert_x ? -100 : 100),
        controller::axis(raw.left_y, preview) * (preview.invert_y ? -100 : 100));
    SetDlgItemTextW(window, IDC_CONTROLLER_LIVE, values);
    if (!dialog.capturing || !dialog.active) {
        return;
    }
    if (dialog.player == input::no_device) {
        dialog.player = player;
    }
    const auto threshold = preview.trigger_threshold;
    const bool neutral =
        !raw.buttons && raw.left_trigger <= threshold && raw.right_trigger <= threshold &&
        std::abs(static_cast<int>(raw.left_x)) <= static_cast<int>(preview.deadzone) &&
        std::abs(static_cast<int>(raw.left_y)) <= static_cast<int>(preview.deadzone);
    if (!dialog.neutral) {
        if (neutral) {
            dialog.neutral = true;
            status(window, dialog, L"Press one button or trigger to assign it.");
        }
        return;
    }
    unsigned count = 0;
    auto binding = controller::Binding::A;
    for (unsigned index = 0; index < controller::binding_count; ++index) {
        const auto value = static_cast<controller::Binding>(index);
        const bool pressed = value == controller::Binding::LeftTrigger
                                 ? raw.left_trigger > threshold
                             : value == controller::Binding::RightTrigger
                                 ? raw.right_trigger > threshold
                                 : (raw.buttons & controller::binding_masks[index]) != 0;
        if (pressed) {
            ++count;
            binding = value;
        }
    }
    if (count > 1) {
        dialog.neutral = false;
        status(window, dialog, L"Release all controls, then press one button.");
    } else if (count == 1) {
        controller::bind(dialog.profile, static_cast<controller::Action>(action(window)), binding);
        refresh_bindings(window, dialog);
        stop_capture(window, dialog, L"Button assigned. The previous assignment was swapped.");
    }
}

INT_PTR CALLBACK dialog_proc(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    auto* dialog = reinterpret_cast<Dialog*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_SETCURSOR) {
        SendMessageW(GetParent(window), RegisterWindowMessageW(L"XFilesEnhancement.ToolCursor"), 5,
                     0);
    }
    if (message == WM_INITDIALOG) {
        dialog = reinterpret_cast<Dialog*>(data);
        SetWindowLongPtrW(window, DWLP_USER, data);
        center_dialog(window);
        ui::translate_dialog(window, dialog->language);
        for (const auto* binding : controller::binding_names) {
            SendDlgItemMessageW(window, IDC_CONTROLLER_BINDING, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(text(*dialog, binding)));
        }
        for (const auto* curve : controller::curve_names) {
            SendDlgItemMessageW(window, IDC_CONTROLLER_CURVE, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(text(*dialog, curve)));
        }
        populate(window, *dialog);
        poll_input(window, *dialog);
        SetTimer(window, input_timer, 50, nullptr);
        return TRUE;
    }
    if (!dialog) {
        return FALSE;
    }
    if (message == WM_TIMER && parameter == input_timer) {
        poll_input(window, *dialog);
        return TRUE;
    }
    if (message == WM_ACTIVATE) {
        dialog->active = LOWORD(parameter) != WA_INACTIVE;
        if (dialog->capturing && !dialog->active) {
            stop_capture(window, *dialog, L"Capture cancelled.");
        }
    }
    if (message == WM_DESTROY) {
        KillTimer(window, input_timer);
    }
    if (message == WM_CLOSE) {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (message != WM_COMMAND) {
        return FALSE;
    }
    switch (LOWORD(parameter)) {
        case IDC_CONTROLLER_ACTIONS:
            if (HIWORD(parameter) == LBN_SELCHANGE) {
                select_binding(window, *dialog);
            }
            return TRUE;
        case IDC_CONTROLLER_BINDING:
            if (HIWORD(parameter) == CBN_SELCHANGE) {
                const auto binding =
                    SendDlgItemMessageW(window, IDC_CONTROLLER_BINDING, CB_GETCURSEL, 0, 0);
                if (binding >= 0 && binding < static_cast<LRESULT>(controller::binding_count)) {
                    controller::bind(dialog->profile,
                                     static_cast<controller::Action>(action(window)),
                                     static_cast<controller::Binding>(binding));
                    refresh_bindings(window, *dialog);
                    status(window, *dialog,
                           L"Button assigned. The previous assignment was swapped.");
                }
            }
            return TRUE;
        case IDC_CONTROLLER_CAPTURE:
            if (dialog->capturing) {
                stop_capture(window, *dialog, L"Capture cancelled.");
            } else {
                dialog->capturing = true;
                dialog->neutral = false;
                dialog->player = input::no_device;
                EnableWindow(GetDlgItem(window, IDC_CONTROLLER_ACTIONS), FALSE);
                EnableWindow(GetDlgItem(window, IDC_CONTROLLER_BINDING), FALSE);
                SetDlgItemTextW(window, IDC_CONTROLLER_CAPTURE, text(*dialog, L"Cancel capture"));
                status(window, *dialog, L"Release all controls, then press one button.");
            }
            return TRUE;
        case IDC_CONTROLLER_RESET:
            stop_capture(window, *dialog, L"Recommended settings restored in this dialog.");
            dialog->profile = {};
            populate(window, *dialog);
            return TRUE;
        case IDOK: {
            auto accepted = dialog->profile;
            const auto invalid = calibration(window, accepted);
            if (invalid) {
                stop_capture(window, *dialog, L"Enter a value in the range shown.");
                SetFocus(GetDlgItem(window, invalid));
                if (invalid != IDC_CONTROLLER_CURVE) {
                    SendDlgItemMessageW(window, invalid, EM_SETSEL, 0, -1);
                }
                return TRUE;
            }
            dialog->outer.controller_profile = accepted;
            EndDialog(window, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(window, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

}

void show_controller_dialog(HWND owner, HMODULE module, Settings& draft) {
    Dialog dialog{draft, controller::normalize(draft.controller_profile), ui::language()};
    platform::ToolTheme theme(module);
    if (DialogBoxParamW(module, MAKEINTRESOURCEW(IDD_CONTROLLER_CONFIG), owner, dialog_proc,
                        reinterpret_cast<LPARAM>(&dialog)) == -1) {
        throw std::runtime_error("Cannot open controller settings");
    }
}

}
