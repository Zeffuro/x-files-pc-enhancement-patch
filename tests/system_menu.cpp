#include "enhancements/ui/system_menu.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace enhancements::system_menu;
constexpr UINT unrelated_command = 0x3210;
ui::Language selected = ui::Language::English;
unsigned refreshes = 0;
bool refreshed = false;

void require(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM word, LPARAM data) {
    if (message == WM_INITMENU || (message == WM_INITMENUPOPUP && HIWORD(data))) {
        ++refreshes;
        refreshed = refresh(window, selected);
    }
    return DefWindowProcW(window, message, word, data);
}

struct Window {
    HWND handle = nullptr;

    Window() {
        WNDCLASSW type{};
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpfnWndProc = window_proc;
        type.lpszClassName = L"XFilesSystemMenuTest";
        require(RegisterClassW(&type) != 0, "Could not register hidden window");
        handle = CreateWindowExW(0, type.lpszClassName, L"System menu test", WS_OVERLAPPEDWINDOW, 0,
                                 0, 100, 100, nullptr, nullptr, type.hInstance, nullptr);
        require(handle != nullptr && !IsWindowVisible(handle), "Could not create hidden window");
    }

    ~Window() {
        if (handle) {
            DestroyWindow(handle);
        }
        UnregisterClassW(L"XFilesSystemMenuTest", GetModuleHandleW(nullptr));
    }
};

struct Item {
    UINT id, type, state;
    ULONG_PTR data;
    HMENU submenu;
    std::wstring text;
    bool operator==(const Item&) const = default;
};

std::vector<Item> snapshot(HMENU menu) {
    const auto count = GetMenuItemCount(menu);
    require(count >= 0, "Could not inspect system menu");
    std::vector<Item> result;
    for (int index = 0; index < count; ++index) {
        wchar_t text[256]{};
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_DATA | MIIM_SUBMENU | MIIM_STRING;
        item.dwTypeData = text;
        item.cch = static_cast<UINT>(std::size(text));
        require(GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &item) != FALSE,
                "Could not read system menu item");
        result.push_back({item.wID, item.fType, item.fState, item.dwItemData, item.hSubMenu, text});
    }
    return result;
}

unsigned occurrences(HMENU menu, UINT id) {
    unsigned count = 0;
    for (const auto& item : snapshot(menu)) {
        count += item.id == id;
    }
    return count;
}

unsigned separators(HMENU menu) {
    unsigned count = 0;
    for (const auto& item : snapshot(menu)) {
        count += (item.type & MFT_SEPARATOR) != 0;
    }
    return count;
}

void preserve(HMENU menu, const std::vector<Item>& expected) {
    const auto actual = snapshot(menu);
    for (const auto& original : expected) {
        if (original.type & MFT_SEPARATOR) {
            continue;
        }
        unsigned matches = 0;
        for (const auto& item : actual) {
            matches += item == original;
        }
        require(matches == 1, "Refresh changed or duplicated an unrelated system menu item");
    }
}

void initialize(HWND window, bool popup = false) {
    const auto before = refreshes;
    refreshed = false;
    const auto menu = GetSystemMenu(window, FALSE);
    SendMessageW(window, popup ? WM_INITMENUPOPUP : WM_INITMENU, reinterpret_cast<WPARAM>(menu),
                 popup ? MAKELPARAM(0, TRUE) : 0);
    require(refreshes == before + 1 && refreshed, "Menu initialization did not refresh commands");
    require(!IsWindowVisible(window), "Menu refresh displayed the hidden test window");
}

struct Labels {
    ui::Language language;
    const wchar_t* settings;
    const wchar_t* inspector;
};

constexpr std::array labels{
    Labels{ui::Language::English, L"Enhancements...\tF10", L"Developer tools...\tCtrl+F11"},
    Labels{ui::Language::German, L"Erweiterungen...\tF10", L"Entwicklerwerkzeuge...\tCtrl+F11"},
    Labels{ui::Language::French, L"Améliorations...\tF10", L"Outils de développement...\tCtrl+F11"},
    Labels{ui::Language::Spanish, L"Mejoras...\tF10", L"Herramientas de desarrollo...\tCtrl+F11"},
    Labels{ui::Language::Italian, L"Migliorie...\tF10", L"Strumenti di sviluppo...\tCtrl+F11"},
    Labels{ui::Language::Japanese, L"拡張設定...\tF10", L"開発者ツール...\tCtrl+F11"},
    Labels{ui::Language::Russian, L"Настройки...\tF10", L"Инструменты разработчика...\tCtrl+F11"},
};
static_assert(labels.size() == ui::language_count);

void verify_labels(HMENU menu, const Labels& expected) {
    require(occurrences(menu, settings_command) == 1 && occurrences(menu, inspector_command) == 1,
            "Custom system menu commands were missing or duplicated");
    for (const auto& item : snapshot(menu)) {
        if (item.id == settings_command) {
            require(item.text == expected.settings,
                    "Settings system menu label has wrong language");
        }
        if (item.id == inspector_command) {
            require(item.text == expected.inspector,
                    "Inspector system menu label has wrong language");
        }
    }
}

void exercise(HWND window) {
    auto menu = GetSystemMenu(window, FALSE);
    require(menu != nullptr, "Hidden window has no system menu");
    require(AppendMenuW(menu, MF_STRING | MF_CHECKED | MF_GRAYED, unrelated_command,
                        L"Unrelated custom command") != FALSE,
            "Could not append unrelated menu item");
    MENUITEMINFOW custom{sizeof(custom)};
    custom.fMask = MIIM_DATA;
    custom.dwItemData = 0x13579;
    require(SetMenuItemInfoW(menu, unrelated_command, FALSE, &custom) != FALSE,
            "Could not set unrelated menu data");
    auto originals = snapshot(menu);
    auto expected_separators = separators(menu) + 1;
    auto expected_count = GetMenuItemCount(menu) + 3;

    for (const auto& expected : labels) {
        selected = expected.language;
        initialize(window);
        verify_labels(menu, expected);
        preserve(menu, originals);
        require(separators(menu) == expected_separators && GetMenuItemCount(menu) == expected_count,
                "Language refresh added duplicate commands or separators");
        const auto once = snapshot(menu);
        initialize(window, true);
        initialize(window);
        require(snapshot(menu) == once, "Repeated initialization changed the menu");
    }

    for (const auto command : {settings_command, inspector_command}) {
        require(DeleteMenu(menu, command, MF_BYCOMMAND) != FALSE,
                "Could not remove custom command");
        const auto before = refreshes;
        SendMessageW(window, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(menu), 0);
        require(refreshes == before && occurrences(menu, command) == 0,
                "Ordinary popup initialization refreshed the system menu");
        initialize(window, true);
        verify_labels(menu, labels.back());
        preserve(menu, originals);
        require(separators(menu) == expected_separators && GetMenuItemCount(menu) == expected_count,
                "Partial removal repair changed unrelated items or separators");
    }

    require(DeleteMenu(menu, settings_command, MF_BYCOMMAND) != FALSE &&
                DeleteMenu(menu, inspector_command, MF_BYCOMMAND) != FALSE,
            "Could not remove both custom commands");
    initialize(window);
    verify_labels(menu, labels.back());
    preserve(menu, originals);
    require(separators(menu) == expected_separators && GetMenuItemCount(menu) == expected_count,
            "Repair after both removals duplicated the separator");

    require(GetSystemMenu(window, TRUE) == nullptr,
            "Native menu reset returned an unexpected handle");
    menu = GetSystemMenu(window, FALSE);
    require(menu != nullptr && occurrences(menu, settings_command) == 0 &&
                occurrences(menu, inspector_command) == 0 &&
                occurrences(menu, unrelated_command) == 0,
            "Native reset did not restore the standard menu");
    originals = snapshot(menu);
    expected_separators = separators(menu) + 1;
    expected_count = GetMenuItemCount(menu) + 3;
    selected = ui::Language::English;
    initialize(window, true);
    verify_labels(menu, labels.front());
    preserve(menu, originals);
    require(separators(menu) == expected_separators && GetMenuItemCount(menu) == expected_count,
            "Refresh after native reset failed to restore custom menu items");
    const auto restored = snapshot(menu);
    initialize(window);
    require(snapshot(menu) == restored, "Refresh after native reset was not idempotent");
}
}

int main() {
    try {
        require(!refresh(nullptr, ui::Language::English),
                "Null window unexpectedly had a system menu");
        Window window;
        exercise(window.handle);
        std::cout << "Hidden Win32 system menu reset, repair and seven-language checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
