#include "enhancements/ui/settings_tabs.h"
#include "enhancements/ui/resources.h"
#include "localization/ui.h"

#include <commctrl.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

constexpr std::array groups{IDC_SETTINGS_DISPLAY, IDC_SETTINGS_CONTROLLER, IDC_SETTINGS_SUBTITLES,
                            IDC_SETTINGS_GAME};
constexpr std::array first_controls{IDC_DISPLAY_MODE, IDC_GAMEPAD, IDC_CAPTIONS, IDC_MENU_BLACK};
constexpr std::array labels{L"Display and audio", L"Controller", L"Subtitles", L"Game"};

INT_PTR CALLBACK dialog_proc(HWND, UINT, WPARAM, LPARAM) {
    return FALSE;
}

void check_language(ui::Language language) {
    const auto window = CreateDialogParamW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ENHANCEMENTS), nullptr, dialog_proc, 0);
    require(window != nullptr, "Cannot create the actual settings resource.");

    struct Cleanup {
        HWND window;

        ~Cleanup() {
            DestroyWindow(window);
        }
    } cleanup{window};

    ui::translate_dialog(window, language);
    std::array<std::vector<HWND>, 4> pages;
    unsigned page = 4;
    for (auto child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const auto id = GetDlgCtrlID(child);
        for (unsigned index = 0; index < groups.size(); ++index) {
            if (id == groups[index]) {
                page = index;
            }
        }
        if (id == IDC_BUILD_VERSION) {
            page = 4;
        }
        if (page < pages.size()) {
            pages[page].push_back(child);
        }
    }
    const auto spring = GetDlgItem(window, IDC_SPRING_CURSOR);
    EnableWindow(spring, FALSE);
    CheckDlgButton(window, IDC_GAMEPAD, BST_UNCHECKED);
    SetDlgItemInt(window, IDC_CAPTION_SCALE, 147, FALSE);
    enhancements::SettingsTabs tabs;
    tabs.initialize(window, language);
    const auto tab = GetDlgItem(window, IDC_SETTINGS_TABS);
    require(TabCtrl_GetItemCount(tab) == 4, "Settings lost a tab.");
    for (unsigned index = 0; index < pages.size(); ++index) {
        wchar_t text[128]{};
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = text;
        item.cchTextMax = 128;
        require(TabCtrl_GetItem(tab, static_cast<int>(index), &item), "Cannot read tab label.");
        require(std::wstring(text) == ui::translate(labels[index], language),
                "Tab label was not localized.");
        RECT label{};
        require(TabCtrl_GetItemRect(tab, static_cast<int>(index), &label), "Missing tab bounds.");
        RECT area{};
        GetClientRect(tab, &area);
        require(label.right <= area.right && label.top < 25,
                "Localized tabs overflowed onto a second row.");
        require(pages[index].size() >= 8, "A settings page lost controls or anonymous labels.");
    }
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        for (unsigned selected = 0; selected < pages.size(); ++selected) {
            tabs.select(selected);
            require(TabCtrl_GetCurSel(tab) == static_cast<int>(selected), "Wrong selected tab.");
            for (unsigned index = 0; index < pages.size(); ++index) {
                for (const auto child : pages[index]) {
                    const auto visible = (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) != 0;
                    require(visible == (index == selected),
                            "Hidden page leaked a control or label.");
                }
            }
            const auto first = GetDlgItem(window, first_controls[selected]);
            require(first != nullptr, "Page's first control was lost.");
            const auto next = GetNextDlgTabItem(window, tab, FALSE);
            require(next == first, "Tab order enters the wrong page.");
            for (const auto id : {IDC_TOOLS, IDC_GITHUB, IDOK, IDCANCEL, IDC_BUILD_VERSION}) {
                require(GetWindowLongPtrW(GetDlgItem(window, id), GWL_STYLE) & WS_VISIBLE,
                        "Global footer disappeared on a tab.");
            }
        }
    }
    require(!IsWindowEnabled(spring), "Switching tabs enabled a dependent disabled control.");
    require(IsDlgButtonChecked(window, IDC_GAMEPAD) == BST_UNCHECKED &&
                GetDlgItemInt(window, IDC_CAPTION_SCALE, nullptr, FALSE) == 147,
            "Switching tabs discarded unapplied edits.");
    RECT bounds{};
    GetWindowRect(window, &bounds);
    require(bounds.bottom - bounds.top < 650, "Settings dialog no longer fits laptop height.");
    tabs.select(9);
    require(TabCtrl_GetCurSel(tab) == 3, "Invalid page selection changed the current page.");
    BYTE prior_keys[256]{};
    require(GetKeyboardState(prior_keys), "Cannot preserve keyboard state.");

    struct KeyboardRestore {
        BYTE* keys;

        ~KeyboardRestore() {
            SetKeyboardState(keys);
        }
    } restore_keys{prior_keys};

    BYTE keys[256]{};
    keys[VK_CONTROL] = 0x80;
    constexpr std::array targets{std::pair{IDC_SETTINGS_TABS, 3u}, std::pair{IDC_GAMEPAD, 1u},
                                 std::pair{IDC_CAPTION_SCALE, 2u}, std::pair{IDOK, 3u},
                                 std::pair{0, 1u}};
    constexpr std::array shortcuts{std::pair{VK_NEXT, VK_PRIOR}, std::pair{VK_TAB, VK_TAB}};
    for (const auto& [forward_key, reverse_key] : shortcuts) {
        for (const auto& [control, selected] : targets) {
            tabs.select(selected);
            const auto target = control ? GetDlgItem(window, control) : window;
            SetFocus(control ? target : tab);
            MSG key{};
            key.hwnd = target;
            key.message = WM_KEYDOWN;
            key.wParam = static_cast<WPARAM>(forward_key);
            key.lParam = 1;
            keys[VK_SHIFT] = 0;
            require(SetKeyboardState(keys), "Cannot test local keyboard navigation.");
            const auto code =
                SendMessageW(target, WM_GETDLGCODE, key.wParam, reinterpret_cast<LPARAM>(&key));
            require(code & (forward_key == VK_TAB ? DLGC_WANTTAB : DLGC_WANTMESSAGE),
                    "Settings did not request the page shortcut message.");
            if (!IsDialogMessageW(window, &key)) {
                TranslateMessage(&key);
                DispatchMessageW(&key);
            }
            require(TabCtrl_GetCurSel(tab) == static_cast<int>((selected + 1) % 4),
                    "Forward settings shortcut failed through dialog message processing.");
            key.hwnd = control ? GetFocus() : window;
            key.wParam = static_cast<WPARAM>(reverse_key);
            keys[VK_SHIFT] = reverse_key == VK_TAB ? 0x80 : 0;
            require(SetKeyboardState(keys), "Cannot test reverse local keyboard navigation.");
            if (!IsDialogMessageW(window, &key)) {
                TranslateMessage(&key);
                DispatchMessageW(&key);
            }
            require(TabCtrl_GetCurSel(tab) == static_cast<int>(selected),
                    "Reverse settings shortcut failed through dialog message processing.");
        }
    }
    keys[VK_CONTROL] = keys[VK_SHIFT] = 0;
    require(SetKeyboardState(keys), "Cannot test ordinary Tab navigation.");
    SetFocus(tab);
    MSG ordinary{};
    ordinary.hwnd = tab;
    ordinary.message = WM_KEYDOWN;
    ordinary.wParam = VK_TAB;
    ordinary.lParam = 1;
    require(IsDialogMessageW(window, &ordinary) && TabCtrl_GetCurSel(tab) == 1 &&
                GetFocus() == GetDlgItem(window, IDC_GAMEPAD),
            "Ordinary Tab no longer enters the selected page.");
}
}

int main() {
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_TAB_CLASSES};
        require(InitCommonControlsEx(&controls), "Cannot initialize common controls.");
        for (unsigned language = 0; language < 6; ++language) {
            check_language(static_cast<ui::Language>(language));
        }
        std::cout << "All settings pages, tab order, translations and retained edits passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
