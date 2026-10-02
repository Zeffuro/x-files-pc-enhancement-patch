#include "devtools/database/browser.h"
#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::wstring text(HWND window) {
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
    value.resize(wcslen(value.c_str()));
    return value;
}
}

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-text-browser-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"XFILES.HDB";
    HWND parent = nullptr;
    try {
        std::filesystem::create_directories(root);
        const auto write = [&](const std::string& source) {
            std::vector<unsigned char> bytes(512);
            bytes[3] = 5;
            bytes[30] = 1;
            std::copy(source.begin(), source.end(), bytes.begin() + 64);
            bytes[64 + source.size()] = 0xc2;
            std::ofstream(path, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        write("<HTML>\n\t<A HREF=\"messages/digest564\">\r\nOffice Tracker</A>\r</HTML>");
        const auto module = GetModuleHandleW(nullptr);
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800, nullptr,
                                 nullptr, module, nullptr);
        require(parent != nullptr, "Cannot create test host");
        const auto browser = devtools::create_database_browser(
            parent, module, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), root, {}, {},
            path);
        const auto modes = GetDlgItem(browser, 3100);
        for (int i = 0; i < SendMessageW(modes, CB_GETCOUNT, 0, 0); ++i) {
            if (SendMessageW(modes, CB_GETITEMDATA, i, 0) == 2) {
                SendMessageW(modes, CB_SETCURSEL, i, 0);
            }
        }
        SendMessageW(browser, WM_COMMAND, MAKEWPARAM(3100, CBN_SELCHANGE), 0);
        SetWindowTextW(GetDlgItem(browser, 3101), L"Office Tracker");
        const auto list = GetDlgItem(browser, 3105);
        require(ListView_GetItemCount(list) == 1, "Multiline fragment not searchable");
        wchar_t summary[512]{};
        ListView_GetItemText(list, 0, 3, summary, 512);
        require(std::wstring(summary).find_first_of(L"\r\n\t") == std::wstring::npos,
                "Text candidate preview contains control glyphs");
        const auto select = [&] {
            ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        };
        select();
        require(text(GetDlgItem(browser, 3114)).find(L"Unverified text fragment") !=
                    std::wstring::npos,
                "Candidate presented as a complete record");
        require(text(GetDlgItem(browser, 3117)) ==
                    L"<HTML>\r\n\t<A HREF=\"messages/digest564\">\r\nOffice Tracker</A>\r\n</HTML>",
                "Text pane lost source or line breaks");
        const auto tabs = GetDlgItem(browser, 3112);
        require(TabCtrl_GetItemCount(tabs) == 3, "Text fragment has no Text tab");
        TCITEMW tab{};
        tab.mask = TCIF_PARAM;
        require(TabCtrl_GetItem(tabs, 2, &tab) && tab.lParam == 3,
                "Text fragment tab has wrong identity");
        TabCtrl_SetCurSel(tabs, 2);
        SendMessageW(browser, WM_SIZE, 0, 0);
        require((GetWindowLongW(GetDlgItem(browser, 3117), GWL_STYLE) & WS_VISIBLE) &&
                    !(GetWindowLongW(GetDlgItem(browser, 3106), GWL_STYLE) & WS_VISIBLE),
                "Text pane does not replace raw pane");
        SetWindowTextW(GetDlgItem(browser, 3101), L"messages/dige");
        write("<LI><B><A HREF=\"messages/dige");
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        select();
        require(text(GetDlgItem(browser, 3117)) == L"<LI><B><A HREF=\"messages/dige",
                "Binary boundary was joined into HTML or stale content retained");
        SetWindowTextW(GetDlgItem(browser, 3101), L"not-a-match");
        require(ListView_GetItemCount(list) == 0 && text(GetDlgItem(browser, 3117)).empty() &&
                    TabCtrl_GetItemCount(tabs) == 2,
                "Empty search retained stale text/tab");
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Database text fragment controls passed\n";
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
