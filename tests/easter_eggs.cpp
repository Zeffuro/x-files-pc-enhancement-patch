#include "devtools/easter_eggs.h"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::wstring text(HWND window) {
    std::wstring result(GetWindowTextLengthW(window) + 1, L'\0');
    result.resize(GetWindowTextW(window, result.data(), static_cast<int>(result.size())));
    return result;
}

void checks(HWND parent, bool clipboard) {
    std::wstring used;
    std::uint32_t variable = 0;
    auto shortcut = devtools::EasterEggShortcut::none;
    const auto panel = devtools::create_easter_eggs(
        parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        {[&used](std::wstring_view code) { used = code; },
         [&variable](std::uint32_t id) { variable = id; },
         [&shortcut](devtools::EasterEggShortcut value) { shortcut = value; }});
    const auto search = GetDlgItem(panel, 5101), list = GetDlgItem(panel, 5102);
    const auto details = GetDlgItem(panel, 5103), copy = GetDlgItem(panel, 5104);
    require(panel && search && list && details && copy, "Missing easter egg controls");
    require(SendMessageW(list, LB_GETCOUNT, 0, 0) ==
                static_cast<LRESULT>(devtools::easter_eggs().size()),
            "Incomplete catalog");
    SetWindowTextW(search, L"THE NATURE OF INFINITY");
    require(SendMessageW(list, LB_GETCOUNT, 0, 0) == 1 && IsWindowEnabled(copy),
            "Code search must ignore case and retain a copyable selection");
    require(text(details).find(L"the nature of infinity is paradox") != std::wstring::npos,
            "Selected instructions do not contain the exact code");
    require(IsWindowEnabled(GetDlgItem(panel, 5105)), "PDA cheat is not actionable");
    SendMessageW(panel, WM_COMMAND, 5105, 0);
    require(used == L"the nature of infinity is paradox", "Use dispatched the wrong cheat");
    used.clear();
    devtools::enable_easter_egg_use(panel, false);
    SendMessageW(panel, WM_COMMAND, 5105, 0);
    require(used.empty() && !IsWindowEnabled(GetDlgItem(panel, 5105)),
            "Unavailable cheat still dispatches");
    devtools::enable_easter_egg_use(panel, true);
    if (clipboard) {
        SendMessageW(panel, WM_COMMAND, 5104, 0);
        require(OpenClipboard(parent), "Cannot inspect copied code");
        const auto data = GetClipboardData(CF_UNICODETEXT);
        const auto* value = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
        const bool correct = value && std::wstring(value) == L"the nature of infinity is paradox";
        if (value) {
            GlobalUnlock(data);
        }
        CloseClipboard();
        require(correct, "Clipboard includes extra text or the wrong code");
    }
    SetWindowTextW(search, L"infinity");
    require(text(details).find(L"the nature of infinity is paradox") != std::wstring::npos,
            "Filtering lost the selected entry");
    SetWindowTextW(search, L"no such secret exists");
    require(SendMessageW(list, LB_GETCOUNT, 0, 0) == 0 && !IsWindowEnabled(copy),
            "Empty search retained a stale copy target");
    used.clear();
    SendMessageW(panel, WM_COMMAND, 5105, 0);
    require(used.empty(), "Empty selection dispatched a stale cheat");
    SetWindowTextW(search, L"briefly flash words");
    require(SendMessageW(list, LB_GETCOUNT, 0, 0) == 1 && !IsWindowEnabled(copy),
            "Instruction-only secret has an invalid code action");
    require(!IsWindowEnabled(GetDlgItem(panel, 5105)), "Guide-only entry is actionable");
    SetWindowTextW(search, L"asta me up");
    require(IsWindowEnabled(GetDlgItem(panel, 5106)), "Score has no variable path action");
    SendMessageW(panel, WM_COMMAND, 5106, 0);
    require(variable == 1411, "Wrong Asta variable path target");
    SetWindowTextW(search, L"visit the developer office");
    require(SendMessageW(list, LB_GETCOUNT, 0, 0) == 1 && !IsWindowEnabled(copy) &&
                IsWindowEnabled(GetDlgItem(panel, 5105)),
            "Studio shortcut must be actionable without a typed code");
    SendMessageW(panel, WM_COMMAND, 5105, 0);
    require(shortcut == devtools::EasterEggShortcut::studio,
            "Studio shortcut dispatched a PDA cheat");
    shortcut = devtools::EasterEggShortcut::none;
    devtools::enable_easter_egg_use(panel, false);
    SendMessageW(panel, WM_COMMAND, 5105, 0);
    require(shortcut == devtools::EasterEggShortcut::none &&
                !IsWindowEnabled(GetDlgItem(panel, 5105)),
            "Unavailable studio shortcut still dispatches");
    devtools::enable_easter_egg_use(panel, true);
    SetWindowTextW(search, L"");
    SendMessageW(list, LB_SETCURSEL, 1, 0);
    SendMessageW(panel, WM_COMMAND, MAKEWPARAM(5102, LBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(list));
    require(text(details).find(L"Von") != std::wstring::npos,
            "List selection did not update instructions");
    MoveWindow(panel, 0, 0, 960, 650, FALSE);
    RECT bounds{};
    GetClientRect(panel, &bounds);
    for (auto control : {search, list, details, copy}) {
        RECT child{};
        GetWindowRect(control, &child);
        MapWindowPoints(HWND_DESKTOP, panel, reinterpret_cast<POINT*>(&child), 2);
        require(child.left >= 0 && child.top >= 0 && child.right <= bounds.right &&
                    child.bottom <= bounds.bottom,
                "Controls do not fit the panel");
    }
    DestroyWindow(panel);
    HWND destructive = nullptr;
    destructive = devtools::create_easter_eggs(
        parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        {[&destructive](std::wstring_view) { DestroyWindow(destructive); }, {}});
    SendMessageW(destructive, WM_COMMAND, 5105, 0);
    require(!IsWindow(destructive) && IsWindow(parent),
            "Use callback did not safely destroy its panel");
    const auto reopened = devtools::create_easter_eggs(
        parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    require(reopened != nullptr, "Cannot reopen the panel");
    require(!IsWindowEnabled(GetDlgItem(reopened, 5105)), "Offline panel enabled a live cheat");
    SetWindowTextW(GetDlgItem(reopened, 5101), L"visit the developer office");
    require(!IsWindowEnabled(GetDlgItem(reopened, 5105)),
            "Offline panel enabled the studio shortcut");
    SendMessageW(GetDlgItem(reopened, 5101), WM_KEYDOWN, VK_ESCAPE, 0);
    require(!IsWindow(parent), "Escape did not close the owning tools window");
}
}

int main(int argc, char**) {
    HWND parent = nullptr;
    try {
        parent = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 1000, 800, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
        require(parent != nullptr, "Cannot create hidden test host");
        checks(parent, argc > 1);
        DestroyWindow(parent);
        std::cout << "Easter egg search, selection, layout and lifetime checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
