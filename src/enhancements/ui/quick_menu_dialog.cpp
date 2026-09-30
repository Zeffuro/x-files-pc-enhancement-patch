#include "quick_menu_dialog.h"
#include "resources.h"
#include "settings.h"
#include "localization/ui.h"
#include "platform/tool_theme.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace enhancements {
namespace {

constexpr std::array item_controls{IDC_QUICK_MENU_SAVE, IDC_QUICK_MENU_LOAD,
                                   IDC_QUICK_MENU_TRANSCRIPT, IDC_QUICK_MENU_TWEAKS,
                                   IDC_QUICK_MENU_MENU};

void center_dialog(HWND window) {
    WINDOWINFO owner_info{sizeof(WINDOWINFO)}, dialog_info{sizeof(WINDOWINFO)};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetWindowInfo(GetParent(window), &owner_info) || !GetWindowInfo(window, &dialog_info) ||
        !GetMonitorInfoW(MonitorFromWindow(GetParent(window), MONITOR_DEFAULTTONEAREST),
                         &monitor)) {
        return;
    }
    const auto& owner = owner_info.rcWindow;
    const auto& dialog = dialog_info.rcWindow;
    const auto width = dialog.right - dialog.left;
    const auto height = dialog.bottom - dialog.top;
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

void update_items(HWND window) {
    const auto enabled = IsDlgButtonChecked(window, IDC_QUICK_MENU_ENABLE) == BST_CHECKED;
    for (const auto control : item_controls) {
        EnableWindow(GetDlgItem(window, control), enabled);
    }
}

INT_PTR CALLBACK dialog_proc(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    auto* draft = reinterpret_cast<Settings*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_SETCURSOR) {
        SendMessageW(GetParent(window), RegisterWindowMessageW(L"XFilesEnhancement.ToolCursor"), 5,
                     0);
    }
    if (message == WM_INITDIALOG) {
        draft = reinterpret_cast<Settings*>(data);
        SetWindowLongPtrW(window, DWLP_USER, data);
        center_dialog(window);
        ui::translate_dialog(window);
        CheckDlgButton(window, IDC_QUICK_MENU_ENABLE,
                       draft->quick_menu ? BST_CHECKED : BST_UNCHECKED);
        for (std::size_t index = 0; index < item_controls.size(); ++index) {
            CheckDlgButton(window, item_controls[index],
                           draft->quick_menu_items[index] ? BST_CHECKED : BST_UNCHECKED);
        }
        update_items(window);
        return TRUE;
    }
    if (message != WM_COMMAND || !draft) {
        return FALSE;
    }
    switch (LOWORD(parameter)) {
        case IDC_QUICK_MENU_ENABLE:
            update_items(window);
            return TRUE;
        case IDOK:
            draft->quick_menu = IsDlgButtonChecked(window, IDC_QUICK_MENU_ENABLE) == BST_CHECKED;
            for (std::size_t index = 0; index < item_controls.size(); ++index) {
                draft->quick_menu_items[index] =
                    IsDlgButtonChecked(window, item_controls[index]) == BST_CHECKED;
            }
            EndDialog(window, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(window, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

}

void show_quick_menu_dialog(HWND owner, HMODULE module, Settings& draft) {
    platform::ToolTheme theme(module);
    if (DialogBoxParamW(module, MAKEINTRESOURCEW(IDD_QUICK_MENU), owner, dialog_proc,
                        reinterpret_cast<LPARAM>(&draft)) == -1) {
        throw std::runtime_error("Cannot open quick menu settings");
    }
}
}
