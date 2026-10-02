#include "devtools/database/browser.h"
#include "devtools/database/source.h"
#include "devtools/database/assets.h"
#include <commctrl.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::wstring text(HWND window) {
    std::wstring value(GetWindowTextLengthW(window) + 1, L'\0');
    value.resize(GetWindowTextW(window, value.data(), static_cast<int>(value.size())));
    return value;
}

void search(HWND browser, const wchar_t* query) {
    SetWindowTextW(GetDlgItem(browser, 3101), query);
    require(ListView_GetItemCount(GetDlgItem(browser, 3105)) == 1,
            "Asset query did not resolve exactly one file");
    ListView_SetItemState(GetDlgItem(browser, 3105), 0, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
}

int tab_index(HWND browser, const wchar_t* caption) {
    const auto tabs = GetDlgItem(browser, 3112);
    for (int index = 0; index < TabCtrl_GetItemCount(tabs); ++index) {
        wchar_t label[64]{};
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = label;
        item.cchTextMax = 64;
        if (TabCtrl_GetItem(tabs, index, &item) && wcscmp(label, caption) == 0) {
            return index;
        }
    }
    return -1;
}

void show_tab(HWND browser, const wchar_t* caption) {
    const auto tabs = GetDlgItem(browser, 3112);
    const int index = tab_index(browser, caption);
    require(index >= 0, "Requested asset tab unavailable");
    TabCtrl_SetCurSel(tabs, index);
    NMHDR change{tabs, 3112, TCN_SELCHANGE};
    SendMessageW(browser, WM_NOTIFY, 3112, reinterpret_cast<LPARAM>(&change));
}

void require_tab(HWND browser, const wchar_t* caption) {
    const int index = tab_index(browser, caption);
    require(index >= 0 && TabCtrl_GetCurSel(GetDlgItem(browser, 3112)) == index,
            "Asset navigation discarded the selected tab");
}

struct Fixture {
    std::filesystem::path root;
    HWND parent = nullptr;

    Fixture() {
        root = std::filesystem::temp_directory_path() /
               ("xfiles-asset-browser-" + std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(root), "Cannot create fixture");
        std::filesystem::create_directory(root / "XT");
        std::ofstream(root / "XT/notes.XTX", std::ios::binary) << "Hello\rWorld\nThird\r\nFourth";
        std::ofstream(root / "XT/second.XTX", std::ios::binary) << "Second text";
        std::ofstream(root / "unknown.pic", std::ios::binary) << "UNKNOWN";
        std::ofstream(root / "large.pic", std::ios::binary) << std::string(5000, 'A');
        for (const auto* file :
             {"ddraw.dll", "options.ini", "readme.md", "unrelated.bin", "XFILESE.DLL"}) {
            std::ofstream(root / file, std::ios::binary) << "fixture";
        }
        std::ofstream(root / "broken.XTX", std::ios::binary).write("A\0B", 3);
        std::ofstream(root / "broken.TTR", std::ios::binary) << "not a font";
        std::ofstream(root / "second.TTR", std::ios::binary) << "another invalid font";
        const auto module = GetModuleHandleW(nullptr);
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800, nullptr,
                                 nullptr, module, nullptr);
        require(parent != nullptr, "Cannot create host");
    }

    ~Fixture() {
        if (parent) {
            DestroyWindow(parent);
        }
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
}

int main(int argc, char** argv) {
    try {
        Fixture fixture;
        const auto source = devtools::browser_source(fixture.root);
        require(source.root == std::filesystem::canonical(fixture.root) &&
                    source.database.empty() && source.asset.empty(),
                "Folder without HDB was not accepted");
        const auto direct = devtools::browser_source(fixture.root / "XT/notes.XTX");
        require(direct.asset == L"notes.XTX" && direct.root.filename() == L"XT" &&
                    direct.database.empty(),
                "Direct text asset did not select its own folder");
        bool rejected = false;
        try {
            devtools::browser_source(fixture.root / "missing");
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Missing source was accepted");
        const auto browser =
            devtools::create_database_browser(fixture.parent, GetModuleHandleW(nullptr),
                                              static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                              source.root, {}, {}, {}, true, L"XT/notes.XTX");
        require(browser != nullptr, "Asset-only browser creation failed");
        const auto mode = GetDlgItem(browser, 3100);
        require(SendMessageW(mode, CB_GETITEMDATA, SendMessageW(mode, CB_GETCURSEL, 0, 0), 0) == 6,
                "Asset-only source did not start in Assets");
        require(text(GetDlgItem(browser, 3114)).find(L"XT/notes.XTX") != std::wstring::npos,
                "Direct asset initial selection lost");
        require(text(GetDlgItem(browser, 3117)) == L"Hello\r\nWorld\r\nThird\r\nFourth",
                "XT content or newline normalization failed");
        SendMessageW(browser, WM_COMMAND, 3115, 0);
        require(tab_index(browser, L"Text") >= 0 &&
                    TabCtrl_GetCurSel(GetDlgItem(browser, 3112)) == tab_index(browser, L"Text"),
                "Read text did not open Text tab");
        require(tab_index(browser, L"Links") < 0 && tab_index(browser, L"Font") < 0,
                "Text selection exposed unrelated tabs");
        require(!IsWindowEnabled(GetDlgItem(browser, 3103)) &&
                    !IsWindowEnabled(GetDlgItem(browser, 3104)),
                "Asset exposed HDB offset controls");
        const int text_tabs = TabCtrl_GetItemCount(GetDlgItem(browser, 3112));
        search(browser, L"broken.TTR");
        require(TabCtrl_GetItemCount(GetDlgItem(browser, 3112)) == text_tabs &&
                    tab_index(browser, L"Text") < 0 && tab_index(browser, L"Font") >= 0,
                "Same-count Font transition retained Text tab");
        SendMessageW(browser, WM_COMMAND, 3115, 0);
        require(TabCtrl_GetCurSel(GetDlgItem(browser, 3112)) == tab_index(browser, L"Font"),
                "Show font selected wrong context pane");
        const auto surface = CreateCompatibleDC(nullptr);
        require(surface != nullptr, "Cannot create font tab test surface");
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_STATIC;
        draw.CtlID = 3118;
        draw.hwndItem = GetDlgItem(browser, 3118);
        draw.hDC = surface;
        draw.rcItem = {0, 0, 500, 400};
        require(SendMessageW(browser, WM_DRAWITEM, 3118, reinterpret_cast<LPARAM>(&draw)),
                "Font pane draw dispatch failed");
        DeleteDC(surface);
        search(browser, L"notes.XTX");
        require(tab_index(browser, L"Font") < 0 && tab_index(browser, L"Text") >= 0,
                "Same-count Text transition retained Font tab");
        require_tab(browser, L"Text");
        search(browser, L"second.XTX");
        require_tab(browser, L"Text");
        require(text(GetDlgItem(browser, 3117)) == L"Second text",
                "Remembered tab shows stale text");
        search(browser, L"second.TTR");
        require_tab(browser, L"Font");
        search(browser, L"second.XTX");
        for (const auto* caption : {L"Overview", L"Raw bytes", L"Text"}) {
            show_tab(browser, caption);
            search(browser, L"notes.XTX");
            require_tab(browser, caption);
            SetWindowTextW(GetDlgItem(browser, 3101), L"no matching asset");
            search(browser, L"second.XTX");
            require_tab(browser, caption);
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            require_tab(browser, caption);
            search(browser, L"broken.TTR");
            require_tab(browser, L"Font");
            search(browser, L"second.XTX");
            require_tab(browser, caption);
        }
        search(browser, L"notes.XTX");
        SendMessageW(browser, WM_COMMAND, 3115, 0);
        for (const auto* query : {L"ddraw.dll", L"options.ini", L"readme.md", L"unrelated.bin"}) {
            SetWindowTextW(GetDlgItem(browser, 3101), query);
            require(ListView_GetItemCount(GetDlgItem(browser, 3105)) == 0,
                    "Non-game file appeared in browser");
        }
        search(browser, L"XFILESE.DLL");
        require(text(GetDlgItem(browser, 3114)).find(L"Localization") != std::wstring::npos,
                "Localization DLL omitted or unclassified");
        search(browser, L"unknown.pic");
        require(text(GetDlgItem(browser, 3106)).find(L"55 4e 4b 4e") != std::wstring::npos,
                "Unknown asset raw bytes unavailable");
        require(text(GetDlgItem(browser, 3117)).empty() && tab_index(browser, L"Text") < 0 &&
                    TabCtrl_GetCurSel(GetDlgItem(browser, 3112)) == 0,
                "Text from previous asset leaked into unknown asset");
        require(!IsWindowEnabled(GetDlgItem(browser, 3115)), "Unknown asset advertised decoder");
        search(browser, L"large.pic");
        require(text(GetDlgItem(browser, 3106)).find(L"First 4096 bytes shown") !=
                    std::wstring::npos,
                "Large asset raw view was not bounded");
        search(browser, L"broken.XTX");
        require(text(GetDlgItem(browser, 3117)).find(L"Hello") == std::wstring::npos &&
                    !text(GetDlgItem(browser, 3117)).empty(),
                "Malformed XT lost explicit status");
        search(browser, L"notes.XTX");
        std::filesystem::remove(fixture.root / "XT/notes.XTX");
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        require(ListView_GetItemCount(GetDlgItem(browser, 3105)) == 0,
                "Refresh retained removed asset");
        const auto explicit_browser =
            devtools::create_database_browser(fixture.parent, GetModuleHandleW(nullptr),
                                              static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                              source.root, {}, {}, {}, true, L"unrelated.bin");
        require(text(GetDlgItem(explicit_browser, 3106)).find(L"66 69 78 74") != std::wstring::npos,
                "Explicit file opening lost bounded raw view");
        for (int index = 1; index < argc; ++index) {
            const auto root = std::filesystem::canonical(argv[index]);
            devtools::DatabaseAssetLimits limits;
            limits.include_other_files = true;
            const auto catalog = devtools::database_assets(root, {}, {}, limits);
            require(!catalog.truncated, "Installed asset scan was truncated");
            std::size_t count = 0;
            for (std::filesystem::recursive_directory_iterator it(root), end; it != end; ++it) {
                if (it->is_directory() && it->path().filename().wstring().starts_with(L".")) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file()) {
                    const auto relative = it->path().lexically_relative(root);
                    const auto found = devtools::database_asset_find(catalog, relative);
                    require(found && catalog.assets[*found].present,
                            "Installed file missing from all-file catalog");
                    ++count;
                }
            }
            require(count == catalog.assets.size(), "Installed asset catalog duplicated files");
            std::cout << root << ": " << count << " files browsable without HDB\n";
        }
        std::cout << "Asset opening, game filtering, context tabs, raw limits and refresh passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Asset browser failed: " << error.what() << '\n';
        return 1;
    }
}
