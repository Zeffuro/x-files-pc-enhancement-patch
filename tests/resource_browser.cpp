#include "devtools/database/browser_state.h"
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

struct Fixture {
    std::filesystem::path root;
    HWND parent = nullptr, browser = nullptr;

    Fixture() {
        root = std::filesystem::temp_directory_path() /
               ("xfiles-resource-browser-" + std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(root), "Cannot create fixture");
        wchar_t executable[32768]{};
        require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "Missing executable path");
        require(CopyFileW(executable, (root / "XFILESE.DLL").c_str(), TRUE),
                "Cannot copy resource fixture");
        std::ofstream(root / "test.XTX") << "Other text";
        std::ofstream(root / "broken.TTR") << "not a font";
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent != nullptr, "Cannot create hidden host");
    }

    HWND open(const std::filesystem::path& path) {
        if (browser) {
            DestroyWindow(browser);
        }
        browser = devtools::create_database_browser(
            parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
            path.parent_path(), {}, {}, {}, true, path.filename());
        require(browser != nullptr, "Cannot create browser");
        return browser;
    }

    ~Fixture() {
        DestroyWindow(parent);
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

void choose(HWND browser, const wchar_t* query) {
    using namespace devtools::database_browser;
    SetWindowTextW(GetDlgItem(browser, search_id), query);
    require(ListView_GetItemCount(GetDlgItem(browser, list_id)) == 1, "Asset query mismatch");
    ListView_SetItemState(GetDlgItem(browser, list_id), 0, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
}

void checks(Fixture& fixture, const std::filesystem::path& path, bool synthetic, bool clipboard) {
    using namespace devtools::database_browser;
    const auto decoded = game_assets::load_resource_strings(path);
    require(decoded.valid && !decoded.strings.empty(), "Fixture did not decode");
    const auto browser = fixture.open(path);
    auto& state = *reinterpret_cast<Browser*>(GetWindowLongPtrW(browser, GWLP_USERDATA));
    require(state.resource_strings.valid && state.string_rows.size() == decoded.strings.size(),
            "UI omitted resource strings");
    SendMessageW(browser, WM_COMMAND, preview_id, 0);
    require(selected_pane(state) == Pane::strings &&
                (GetWindowLongW(state.strings_list, GWL_STYLE) & WS_VISIBLE) &&
                !(GetWindowLongW(state.content, GWL_STYLE) & WS_VISIBLE) &&
                !(GetWindowLongW(state.font_view, GWL_STYLE) & WS_VISIBLE),
            "Show strings chose an incorrect contextual pane");
    const auto query =
        synthetic ? std::wstring(L"First English") : std::to_wstring(decoded.strings.front().id);
    SetWindowTextW(state.strings_search, query.c_str());
    require(!state.string_rows.empty() && state.string_rows.size() <= decoded.strings.size(),
            "String text filtering failed");
    const auto asset_query = text(state.search);
    const auto rows = state.string_rows;
    BYTE previous[256]{}, keys[256]{};
    require(GetKeyboardState(previous), "Cannot capture test keyboard state");
    keys[VK_CONTROL] = 0x80;
    for (const auto control :
         {state.strings_search, state.strings_list, state.strings_text, state.strings_copy}) {
        SetKeyboardState(keys);
        SetFocus(control);
        MSG message{};
        message.hwnd = control;
        message.message = WM_KEYDOWN;
        message.wParam = 'F';
        if (!IsDialogMessageW(fixture.parent, &message)) {
            DispatchMessageW(&message);
        }
        DWORD start = 0, end = 0;
        SendMessageW(state.search, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                     reinterpret_cast<LPARAM>(&end));
        const bool focused = GetFocus() == state.search && start == 0 && end == asset_query.size();
        SetKeyboardState(previous);
        require(focused && text(state.search) == asset_query &&
                    text(state.strings_search) == query && state.string_rows == rows &&
                    selected_pane(state) == Pane::strings,
                "Ctrl+F in Strings changed either query, the pane or matching strings");
    }
    if (synthetic) {
        require(state.string_rows.size() == 1 && state.string_rows.front()[0] == L"16" &&
                    text(state.strings_text).find(L"Language ID: 1033") != std::wstring::npos,
                "ID/language/selected string text mismatch");
        SetWindowTextW(state.strings_search, L"1036");
        require(state.string_rows.size() == 1 && state.string_rows.front()[0] == L"16",
                "Language filtering failed");
        describe(state);
        require(text(state.strings_search) == L"1036" && state.string_rows.size() == 1,
                "Same-selection refresh discarded string filtering");
        SetWindowTextW(state.strings_search, L"Long text");
        require(text(state.strings_text).find(L"Long text\r\nSecond line") != std::wstring::npos,
                "Full string newlines were not displayed correctly");
    }
    SetWindowTextW(state.strings_search, L"string-with-no-match-812736");
    require(state.string_rows.empty() &&
                text(state.strings_text).find(L"Matching strings: 0") != std::wstring::npos,
            "Empty filter left stale selected text");
    SetWindowTextW(state.strings_search, L"");
    require(state.string_rows.size() == decoded.strings.size(), "Filter clear lost strings");
    if (synthetic) {
        state.resource_strings.strings = {{99, 1033, 0, 0, std::wstring(65535, L'\x0001')}};
        filter_strings(state);
        require(text(state.strings_text).size() < 50000 &&
                    text(state.strings_text).find(L"Long string preview shortened") !=
                        std::wstring::npos,
                "Maximum counted text did not receive a bounded preview");
        if (clipboard) {
            SendMessageW(browser, WM_COMMAND, strings_copy_id, 0);
            require(OpenClipboard(browser), "Cannot inspect copied string");
            const auto memory = GetClipboardData(CF_UNICODETEXT);
            const auto copied = static_cast<const wchar_t*>(GlobalLock(memory));
            std::wstring expected;
            for (int index = 0; index < 65535; ++index) {
                expected += L"\\u0001";
            }
            const bool complete = copied && std::wstring(copied) == expected;
            if (copied) {
                GlobalUnlock(memory);
            }
            CloseClipboard();
            require(complete, "Full string clipboard copy was truncated");
        }
        choose(browser, L"test.XTX");
        require(selected_pane(state) != Pane::strings, "Strings pane survived Text replacement");
        SendMessageW(browser, WM_COMMAND, preview_id, 0);
        require(selected_pane(state) == Pane::text, "Text replacement picked wrong pane");
        choose(browser, L"XFILESE.DLL");
        require(selected_pane(state) == Pane::strings,
                "Localization return forgot its Strings tab");
        SendMessageW(browser, WM_COMMAND, preview_id, 0);
        require(selected_pane(state) == Pane::strings, "Text-to-Strings tab identity failed");
        std::ofstream(path, std::ios::binary | std::ios::trunc) << "MZ broken";
        SendMessageW(browser, WM_COMMAND, refresh_id, 0);
        require(!state.resource_strings.valid && state.string_rows.empty() &&
                    selected_pane(state) == Pane::strings &&
                    text(state.strings_text) == state.resource_strings.status,
                "Malformed replacement left stale decoded strings");
    }
}
}

int main(int argc, char** argv) {
    try {
        Fixture fixture;
        const bool clipboard = argc > 1 && std::string_view(argv[1]) == "--clipboard";
        checks(fixture, fixture.root / "XFILESE.DLL", true, clipboard);
        for (int at = 1; at < argc; ++at) {
            if (std::string_view(argv[at]) != "--clipboard") {
                checks(fixture, std::filesystem::path(argv[at]), false, false);
            }
        }
        std::cout << "Localization string browser checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
