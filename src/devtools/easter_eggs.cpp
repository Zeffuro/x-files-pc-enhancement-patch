#include "easter_eggs.h"
#include <commctrl.h>
#include <algorithm>
#include <cwctype>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace devtools {
namespace {
constexpr wchar_t class_name[] = L"XFilesEasterEggs";

enum { search_id = 5101, list_id, details_id, copy_id, use_id, variable_id };

struct Panel {
    HWND window = nullptr, search = nullptr, list = nullptr, details = nullptr, copy = nullptr;
    HWND heading = nullptr;
    HWND use = nullptr, variable = nullptr, scores = nullptr;
    EasterEggActions actions;
    std::vector<std::size_t> rows;
    bool owned = false;
    bool available = true;
};

std::wstring lower(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return result;
}

std::wstring text(HWND window) {
    std::wstring result(GetWindowTextLengthW(window) + 1, L'\0');
    result.resize(GetWindowTextW(window, result.data(), static_cast<int>(result.size())));
    return result;
}

const EasterEgg* selected(const Panel& panel) {
    const auto row = SendMessageW(panel.list, LB_GETCURSEL, 0, 0);
    return row >= 0 && static_cast<std::size_t>(row) < panel.rows.size()
               ? &easter_eggs()[panel.rows[row]]
               : nullptr;
}

bool usable(const Panel& panel, const EasterEgg* egg) {
    return panel.available && egg &&
           ((egg->shortcut != EasterEggShortcut::none && panel.actions.shortcut) ||
            (panel.actions.use && egg->name.starts_with(L"PDA:") && !egg->code.empty()));
}

void describe(Panel& panel) {
    const auto* egg = selected(panel);
    std::wstring details;
    if (egg) {
        details = std::wstring(egg->name) + L"\r\n\r\n";
        if (!egg->code.empty()) {
            details += L"Code / password / number: " + std::wstring(egg->code) + L"\r\n\r\n";
        }
        if (egg->name.starts_with(L"PDA:") && !egg->code.empty()) {
            details +=
                L"Open the PDA's Puget Sound map, Seattle map or address book. "
                L"Type the code without quotes and press Enter. The text stays invisible. "
                L"Letter case does not matter. Notes and e-mail do not accept cheats.\r\n\r\n";
        }
        details += egg->instructions;
        details += L"\r\n\r\nNot every entry has been tested in the original game. "
                   L"Availability can depend on your edition and current story state. "
                   L"Save before using cheats that change progress.";
    } else {
        details = L"No matching secrets.";
    }
    SetWindowTextW(panel.details, details.c_str());
    EnableWindow(panel.copy, egg && !egg->code.empty());
    EnableWindow(panel.use, usable(panel, egg));
    EnableWindow(panel.variable, panel.actions.variable && egg && egg->variable);
    SetWindowTextW(panel.copy, L"Copy code");
}

void populate(Panel& panel) {
    const auto* previous = selected(panel);
    const auto query = lower(text(panel.search));
    SendMessageW(panel.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(panel.list, LB_RESETCONTENT, 0, 0);
    panel.rows.clear();
    int selection = 0;
    const auto entries = easter_eggs();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& egg = entries[i];
        if (lower(std::wstring(egg.name) + L" " + std::wstring(egg.code) + L" " +
                  std::wstring(egg.instructions))
                .find(query) == std::wstring::npos) {
            continue;
        }
        if (&egg == previous) {
            selection = static_cast<int>(panel.rows.size());
        }
        panel.rows.push_back(i);
        SendMessageW(panel.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(egg.name.data()));
    }
    SendMessageW(panel.list, LB_SETCURSEL, selection, 0);
    SendMessageW(panel.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(panel.list, nullptr, TRUE);
    describe(panel);
}

void copy(Panel& panel) {
    const auto* egg = selected(panel);
    if (!egg || egg->code.empty()) {
        return;
    }
    const auto bytes = (egg->code.size() + 1) * sizeof(wchar_t);
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        throw std::runtime_error("Cannot allocate clipboard text");
    }
    auto* target = static_cast<wchar_t*>(GlobalLock(memory));
    if (!target) {
        GlobalFree(memory);
        throw std::runtime_error("Cannot lock clipboard text");
    }
    std::copy(egg->code.begin(), egg->code.end(), target);
    target[egg->code.size()] = L'\0';
    GlobalUnlock(memory);
    if (!OpenClipboard(panel.window)) {
        GlobalFree(memory);
        throw std::runtime_error("Cannot open the clipboard");
    }
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (!copied) {
        GlobalFree(memory);
        throw std::runtime_error("Cannot copy the code");
    }
    SetWindowTextW(panel.copy, L"Copied");
}

void layout(Panel& panel) {
    RECT bounds{};
    GetClientRect(panel.window, &bounds);
    const int width = std::max(1L, bounds.right), height = std::max(1L, bounds.bottom);
    const int left = std::min(340, width / 2), right = left + 12;
    MoveWindow(panel.heading, 0, 0, width, 42, TRUE);
    MoveWindow(panel.search, 0, 50, left, 28, TRUE);
    MoveWindow(panel.list, 0, 86, left, std::max(1, height - 104), TRUE);
    MoveWindow(panel.details, right, 50, std::max(1, width - right), std::max(1, height - 300),
               TRUE);
    MoveWindow(panel.scores, right, std::max(90, height - 240), std::max(1, width - right), 180,
               TRUE);
    MoveWindow(panel.use, right, std::max(86, height - 48), 126, 30, TRUE);
    MoveWindow(panel.variable, right + 134, std::max(86, height - 48), 126, 30, TRUE);
    MoveWindow(panel.copy, right + 268, std::max(86, height - 48), 126, 30, TRUE);
}

LRESULT CALLBACK keys(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR,
                      DWORD_PTR context) {
    auto& panel = *reinterpret_cast<Panel*>(context);
    if (message == WM_KEYDOWN && value == VK_TAB) {
        SetFocus(GetNextDlgTabItem(GetAncestor(window, GA_ROOT), window,
                                   (GetKeyState(VK_SHIFT) & 0x8000) != 0));
        return 0;
    }
    if (message == WM_KEYDOWN && value == VK_ESCAPE) {
        SendMessageW(GetAncestor(window, GA_ROOT), WM_CLOSE, 0, 0);
        return 0;
    }
    if (message == WM_KEYDOWN && value == 'F' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        SetFocus(panel.search);
        SendMessageW(panel.search, EM_SETSEL, 0, -1);
        return 0;
    }
    return DefSubclassProc(window, message, value, data);
}

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM value, LPARAM data) {
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        panel = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(data)->lpCreateParams);
        panel->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panel));
    }
    if (!panel) {
        return DefWindowProcW(window, message, value, data);
    }
    try {
        if (message == WM_SIZE) {
            layout(*panel);
        } else if (message == WM_COMMAND) {
            if (LOWORD(value) == search_id && HIWORD(value) == EN_CHANGE) {
                populate(*panel);
            } else if (LOWORD(value) == list_id && HIWORD(value) == LBN_SELCHANGE) {
                describe(*panel);
            } else if (LOWORD(value) == copy_id) {
                copy(*panel);
            } else if (LOWORD(value) == use_id) {
                const auto* egg = selected(*panel);
                if (usable(*panel, egg) && egg->shortcut != EasterEggShortcut::none) {
                    auto use = panel->actions.shortcut;
                    const auto shortcut = egg->shortcut;
                    use(shortcut);
                    return 0;
                }
                if (usable(*panel, egg)) {
                    auto use = panel->actions.use;
                    const std::wstring code(egg->code);
                    use(code);
                    return 0;
                }
            } else if (LOWORD(value) == variable_id && panel->actions.variable) {
                const auto* egg = selected(*panel);
                if (egg && egg->variable) {
                    auto inspect = panel->actions.variable;
                    inspect(egg->variable);
                    return 0;
                }
            }
        } else if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            if (panel->owned) {
                delete panel;
            }
        }
    } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "Easter eggs", MB_OK | MB_ICONERROR);
    }
    return DefWindowProcW(window, message, value, data);
}
}

HWND create_easter_eggs(HWND parent, HMODULE module, HFONT font, EasterEggActions actions) {
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = procedure;
    type.hInstance = module;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = class_name;
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("Cannot register the easter egg panel");
    }
    auto pending = std::make_unique<Panel>();
    auto* panel = pending.get();
    panel->actions = std::move(actions);
    const auto window =
        CreateWindowExW(WS_EX_CONTROLPARENT, class_name, L"Easter eggs", WS_CHILD | WS_CLIPCHILDREN,
                        0, 0, 1, 1, parent, nullptr, module, panel);
    if (!window) {
        throw std::runtime_error("Cannot create the easter egg panel");
    }
    panel->owned = true;
    pending.release();
    try {
        const auto child = [&](const wchar_t* kind, const wchar_t* caption, DWORD style,
                               int id = 0) {
            const auto control =
                CreateWindowExW(0, kind, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module, nullptr);
            if (!control) {
                throw std::runtime_error("Cannot create easter egg controls");
            }
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            SetWindowSubclass(control, keys, 1, reinterpret_cast<DWORD_PTR>(panel));
            return control;
        };
        panel->heading = child(L"STATIC",
                               panel->actions.use
                                   ? L"Original secrets (spoilers). Use in game runs the selected "
                                     L"cheat or shortcut during exploration. Save first."
                                   : L"Original secrets (spoilers). Direct use and live scores "
                                     L"are available in the running game's tools.",
                               0);
        panel->search = child(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, search_id);
        SendMessageW(panel->search, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"Search secrets, codes and instructions"));
        panel->list =
            child(L"LISTBOX", L"Secrets",
                  WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, list_id);
        panel->details =
            child(L"EDIT", L"",
                  WS_BORDER | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                  details_id);
        panel->copy = child(L"BUTTON", L"Copy code", WS_TABSTOP | BS_PUSHBUTTON, copy_id);
        panel->use = child(L"BUTTON", L"Use in game", WS_TABSTOP | BS_PUSHBUTTON, use_id);
        panel->variable =
            child(L"BUTTON", L"Variable paths", WS_TABSTOP | BS_PUSHBUTTON, variable_id);
        panel->scores = child(L"EDIT", L"Live scores require a running game.",
                              WS_BORDER | WS_TABSTOP | ES_READONLY | ES_MULTILINE | WS_VSCROLL);
        populate(*panel);
        layout(*panel);
    } catch (...) {
        DestroyWindow(window);
        throw;
    }
    return window;
}

void update_easter_egg_scores(HWND window, std::wstring_view value) {
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (panel && text(panel->scores) != value) {
        SetWindowTextW(panel->scores, std::wstring(value).c_str());
    }
}

void enable_easter_egg_use(HWND window, bool available) {
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (panel) {
        panel->available = available;
        const auto* egg = selected(*panel);
        EnableWindow(panel->use, usable(*panel, egg));
    }
}
}
