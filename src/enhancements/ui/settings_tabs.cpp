#include "settings_tabs.h"
#include "resources.h"
#include "localization/ui.h"

#include <commctrl.h>
#include <stdexcept>

namespace enhancements {
namespace {
constexpr std::array groups{IDC_SETTINGS_DISPLAY, IDC_SETTINGS_CONTROLLER, IDC_SETTINGS_SUBTITLES,
                            IDC_SETTINGS_GAME};
constexpr std::array labels{L"Display and audio", L"Controller", L"Subtitles", L"Game"};

bool page_shortcut(WPARAM key) {
    return key == VK_NEXT || key == VK_PRIOR || key == VK_TAB;
}
}

void SettingsTabs::initialize(HWND dialog, ui::Language language) {
    tab_ = GetDlgItem(dialog, IDC_SETTINGS_TABS);
    for (unsigned page = 0; page < labels.size(); ++page) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(ui::translate(labels[page], language));
        if (TabCtrl_InsertItem(tab_, static_cast<int>(page), &item) == -1) {
            throw std::runtime_error("Cannot initialize settings tabs");
        }
    }
    unsigned page = static_cast<unsigned>(pages_.size());
    // Resource order keeps anonymous labels with their page and the footer outside it.
    for (auto child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const auto id = GetDlgCtrlID(child);
        for (unsigned index = 0; index < groups.size(); ++index) {
            if (id == groups[index]) {
                page = index;
            }
        }
        if (id == IDC_BUILD_VERSION) {
            page = static_cast<unsigned>(pages_.size());
        }
        if (page < pages_.size()) {
            pages_[page].push_back(child);
        }
        if (!SetWindowSubclass(child, keyboard, 1, reinterpret_cast<DWORD_PTR>(this))) {
            throw std::runtime_error("Cannot initialize settings keyboard navigation");
        }
    }
    if (!SetWindowSubclass(dialog, keyboard, 1, reinterpret_cast<DWORD_PTR>(this))) {
        throw std::runtime_error("Cannot initialize settings keyboard navigation");
    }
    SetWindowPos(tab_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    select(0);
}

void SettingsTabs::select(unsigned page) {
    if (page >= pages_.size()) {
        return;
    }
    const auto focus = GetFocus();
    bool hidden_focus = false;
    TabCtrl_SetCurSel(tab_, static_cast<int>(page));
    for (unsigned index = 0; index < pages_.size(); ++index) {
        for (const auto child : pages_[index]) {
            hidden_focus |= index != page && child == focus;
            ShowWindow(child, index == page ? SW_SHOW : SW_HIDE);
        }
    }
    if (hidden_focus) {
        SetFocus(tab_);
    }
    RedrawWindow(GetParent(tab_), nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

LRESULT CALLBACK SettingsTabs::keyboard(HWND window, UINT message, WPARAM parameter, LPARAM data,
                                        UINT_PTR id, DWORD_PTR reference) {
    auto* tabs = reinterpret_cast<SettingsTabs*>(reference);
    if (message == WM_GETDLGCODE && data && (GetKeyState(VK_CONTROL) & 0x8000)) {
        const auto* key = reinterpret_cast<const MSG*>(data);
        if (key->message == WM_KEYDOWN && page_shortcut(key->wParam)) {
            return DefSubclassProc(window, message, parameter, data) | DLGC_WANTMESSAGE |
                   (key->wParam == VK_TAB ? DLGC_WANTTAB : 0);
        }
    }
    if (message == WM_KEYDOWN && page_shortcut(parameter) && (GetKeyState(VK_CONTROL) & 0x8000)) {
        const auto current = TabCtrl_GetCurSel(tabs->tab_);
        const auto backward =
            parameter == VK_PRIOR || (parameter == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
        const auto direction = backward ? 3 : 1;
        tabs->select(static_cast<unsigned>((current + direction) % 4));
        SetFocus(tabs->tab_);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, keyboard, id);
    }
    return DefSubclassProc(window, message, parameter, data);
}

}
