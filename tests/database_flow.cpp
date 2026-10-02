#include "stored_action_browser_fixture.h"
#include <fstream>
#include <iostream>

using namespace stored_action_browser_fixture;

namespace {
void pane(HWND browser, const wchar_t* name) {
    const auto tabs = GetDlgItem(browser, 3112);
    for (int index = 0; index < TabCtrl_GetItemCount(tabs); ++index) {
        wchar_t value[64]{};
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = value;
        item.cchTextMax = 64;
        TabCtrl_GetItem(tabs, index, &item);
        if (wcscmp(value, name) == 0) {
            TabCtrl_SetCurSel(tabs, index);
            NMHDR change{tabs, 3112, TCN_SELCHANGE};
            SendMessageW(browser, WM_NOTIFY, 3112, reinterpret_cast<LPARAM>(&change));
            return;
        }
    }
    throw std::runtime_error("Requested pane is missing");
}

std::wstring pane_name(HWND browser) {
    const auto tabs = GetDlgItem(browser, 3112);
    wchar_t value[64]{};
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = value;
    item.cchTextMax = 64;
    TabCtrl_GetItem(tabs, TabCtrl_GetCurSel(tabs), &item);
    return value;
}

void press(HWND browser, unsigned id) {
    SendMessageW(browser, WM_COMMAND, id, 0);
}

void search_shortcut(HWND parent, HWND browser) {
    const auto search = GetDlgItem(browser, 3101), list = GetDlgItem(browser, 3105);
    const auto query = text(search), title = text(GetDlgItem(browser, 3114));
    const auto pane = pane_name(browser);
    const auto selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    const auto count = ListView_GetItemCount(list);
    BYTE previous[256]{};
    require(GetKeyboardState(previous), "Cannot capture test keyboard state");

    struct KeyboardRestore {
        BYTE* previous;

        ~KeyboardRestore() {
            SetKeyboardState(previous);
        }
    } restore{previous};

    for (auto control = GetWindow(browser, GW_CHILD); control;
         control = GetWindow(control, GW_HWNDNEXT)) {
        const auto style = GetWindowLongW(control, GWL_STYLE);
        if ((style & (WS_VISIBLE | WS_TABSTOP)) != (WS_VISIBLE | WS_TABSTOP) ||
            !IsWindowEnabled(control)) {
            continue;
        }
        for (const int modifiers : {0, 1, 3, 5}) {
            BYTE keys[256]{};
            keys[VK_CONTROL] = modifiers & 1 ? 0x80 : 0;
            keys[VK_SHIFT] = modifiers & 2 ? 0x80 : 0;
            keys[VK_MENU] = modifiers & 4 ? 0x80 : 0;
            require(SetKeyboardState(keys), "Cannot set test keyboard state");
            SetFocus(control);
            MSG message{};
            message.hwnd = control;
            message.message = WM_KEYDOWN;
            message.wParam = 'F';
            if (!IsDialogMessageW(parent, &message)) {
                DispatchMessageW(&message);
            }
            require(GetFocus() == (modifiers == 1 ? search : control),
                    "Search shortcut focus or modifier scope is incorrect");
            if (modifiers == 1) {
                DWORD start = 0, end = 0;
                SendMessageW(search, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                             reinterpret_cast<LPARAM>(&end));
                require(start == 0 && end == query.size(), "Search query was not fully selected");
                require(text(search) == query && text(GetDlgItem(browser, 3114)) == title &&
                            pane_name(browser) == pane && ListView_GetItemCount(list) == count &&
                            ListView_GetNextItem(list, -1, LVNI_SELECTED) == selected,
                        "Search shortcut changed the query, view, selected row or pane");
            }
        }
    }
    require(text(search) == query && text(GetDlgItem(browser, 3114)) == title &&
                pane_name(browser) == pane && ListView_GetItemCount(list) == count &&
                ListView_GetNextItem(list, -1, LVNI_SELECTED) == selected,
            "Search shortcut changed the query, view, selected row or pane");
}

void check(HWND parent, const std::filesystem::path& root) {
    unsigned captures = 0;
    devtools::NativeDatabaseSnapshot snapshot;
    snapshot.available = true;
    snapshot.manager_address = 1;
    devtools::NativeDatabaseObject object;
    object.class_id = 0x35;
    object.id = 42;
    object.description = L"XT/context.xtx";
    object.fields = L"Flow source: 7";
    snapshot.objects.push_back(object);
    const auto browser = devtools::create_database_browser(
        parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        root, {}, [&] {
            ++captures;
            return snapshot;
        });
    MoveWindow(browser, 0, 0, 1180, 780, TRUE);
    const auto list = GetDlgItem(browser, 3105), search = GetDlgItem(browser, 3101);
    const auto database = GetDlgItem(browser, 3132), assets = GetDlgItem(browser, 3129);
    require(database && assets && IsWindowEnabled(database), "Primary navigation is missing");
    SetWindowTextW(search, L"Flow source");
    select(list, 0);
    pane(browser, L"Fields");
    const auto source = text(GetDlgItem(browser, 3114));
    pane(browser, L"Links");
    search_shortcut(parent, browser);
    select(GetDlgItem(browser, 3108), 0);
    press(browser, 3109);
    require(SendMessageW(assets, BM_GETCHECK, 0, 0) == BST_CHECKED &&
                SendMessageW(database, BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
                text(GetDlgItem(browser, 3131)).starts_with(L"Asset files | ") &&
                text(GetDlgItem(browser, 3130)).find(L"switch view") != std::wstring::npos,
            "Following an asset link did not identify the active asset view");
    press(browser, 3110);
    require(text(search) == L"Flow source" && text(GetDlgItem(browser, 3114)) == source &&
                pane_name(browser) == L"Links" && ListView_GetItemCount(list) == 1,
            "Back lost the filtered source, selected record or detail pane");
    press(browser, 3128);
    SetWindowTextW(search, L"400 VCTrigger");
    select(list, 0);
    pane(browser, L"Fields");
    const auto record = text(GetDlgItem(browser, 3114));
    search_shortcut(parent, browser);
    press(browser, 3129);
    require(text(search).empty(), "Database search leaked into asset files");
    SetWindowTextW(search, L"context.xtx");
    select(list, 0);
    press(browser, 3115);
    require(pane_name(browser) == L"Text", "Text asset did not open its content pane");
    const auto asset = text(GetDlgItem(browser, 3114));
    search_shortcut(parent, browser);
    press(browser, 3132);
    require(text(search) == L"400 VCTrigger" && text(GetDlgItem(browser, 3114)) == record &&
                pane_name(browser) == L"Fields" &&
                SendMessageW(database, BM_GETCHECK, 0, 0) == BST_CHECKED &&
                text(GetDlgItem(browser, 3131)).starts_with(L"Database tables | "),
            "Database return lost its query, record or Fields pane");
    press(browser, 3129);
    require(text(search) == L"context.xtx" && text(GetDlgItem(browser, 3114)) == asset &&
                pane_name(browser) == L"Text",
            "Asset return lost its query, selection or content pane");
    select(GetDlgItem(browser, 3127), 2);
    require(SendMessageW(database, BM_GETCHECK, 0, 0) == BST_CHECKED && text(search).empty() &&
                text(GetDlgItem(browser, 3131)).find(L"VCTrigger") != std::wstring::npos &&
                pane_name(browser) == L"Fields",
            "Selecting a database table from assets did not clearly switch to its records");
    press(browser, 3129);
    require(text(search) == L"context.xtx", "Table navigation discarded the asset query");
    press(browser, 3102);
    require(text(GetDlgItem(browser, 3114)) == asset && pane_name(browser) == L"Text",
            "Refresh lost the selected asset or content pane");
    for (const int width : {1180, 1800, 944, 976, 1180}) {
        MoveWindow(browser, 0, 0, width, 780, TRUE);
        RECT search_bounds{}, refresh_bounds{};
        GetWindowRect(search, &search_bounds);
        GetWindowRect(GetDlgItem(browser, 3116), &refresh_bounds);
        require(text(GetDlgItem(browser, 3114)) == asset && pane_name(browser) == L"Text",
                "Resize lost the active asset context");
        require(search_bounds.right < refresh_bounds.left,
                "Asset search overlaps auto-refresh at the inspector minimum width");
    }
    require(captures == 2, "Navigation or search performed native captures");
    DestroyWindow(browser);
}

void scroll_return(HWND parent, const std::filesystem::path& root) {
    devtools::NativeDatabaseSnapshot snapshot;
    snapshot.available = true;
    for (std::uint32_t id = 1000; id < 1425; ++id) {
        devtools::NativeDatabaseObject object;
        object.class_id = 0x53;
        object.id = id;
        snapshot.objects.push_back(object);
    }
    const auto browser = devtools::create_database_browser(
        parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        root, {}, [&] { return snapshot; });
    MoveWindow(browser, 0, 0, 1180, 780, TRUE);
    const auto list = GetDlgItem(browser, 3105);
    select(list, 424);
    ListView_EnsureVisible(list, 424, FALSE);
    ListView_EnsureVisible(list, 0, FALSE);
    require(ListView_GetTopIndex(list) == 0, "Cannot establish offscreen selected row");
    press(browser, 3129);
    choose(browser, 3100, 0);
    require(ListView_GetTopIndex(list) == 0 && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 424,
            "Returning to a view moved its viewport to an offscreen selected row");
    DestroyWindow(browser);
}

void table_panes(HWND parent, const std::filesystem::path& root) {
    const auto browser = devtools::create_database_browser(
        parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        root, {});
    const auto classes = GetDlgItem(browser, 3127), list = GetDlgItem(browser, 3105);
    select(classes, 0);
    require(pane_name(browser) == L"Fields", "First table did not default to Fields");
    for (const auto* name : {L"Fields", L"Raw bytes", L"Overview"}) {
        pane(browser, name);
        search_shortcut(parent, browser);
        for (int index = 0; index < ListView_GetItemCount(classes); ++index) {
            select(classes, index);
            require(pane_name(browser) == name, "Table change lost the chosen database tab");
            for (int row = 0; row < ListView_GetItemCount(list); ++row) {
                select(list, row);
                require(pane_name(browser) == name, "Record change lost the chosen database tab");
            }
        }
        SetWindowTextW(GetDlgItem(browser, 3101), L"no matching record");
        SetWindowTextW(GetDlgItem(browser, 3101), L"");
        select(list, 0);
        require(pane_name(browser) == name, "Empty search discarded the database tab");
        press(browser, 3102);
        require(pane_name(browser) == name, "Refresh discarded the database tab");
        press(browser, 3128);
        require(pane_name(browser) == L"Overview", "Database overview did not open Overview");
        select(classes, 0);
        require(pane_name(browser) == name, "File overview discarded the chosen record tab");
    }
    select(classes, 2);
    pane(browser, L"Links");
    select(classes, 0);
    require(pane_name(browser) == L"Overview", "Unavailable Links tab did not fall back safely");
    press(browser, 3129);
    press(browser, 3132);
    select(classes, 2);
    require(pane_name(browser) == L"Links", "Unavailable tab fallback erased the preference");
    press(browser, 3129);
    SetWindowTextW(GetDlgItem(browser, 3101), L"context.xtx");
    select(list, 0);
    press(browser, 3115);
    select(classes, 2);
    require(pane_name(browser) == L"Links", "Asset tab replaced the remembered database tab");
    select(GetDlgItem(browser, 3108), 0);
    press(browser, 3109);
    pane(browser, L"Raw bytes");
    press(browser, 3110);
    require(pane_name(browser) == L"Links", "Back did not restore its source tab");
    select(classes, 1);
    require(pane_name(browser) == L"Links", "Back did not restore the database tab preference");
    pane(browser, L"Raw bytes");
    press(browser, 3129);
    select(classes, 2);
    require(pane_name(browser) == L"Raw bytes", "Saved asset view restored a stale database tab");
    DestroyWindow(browser);
}

void asset_panes(HWND parent, const std::filesystem::path& root) {
    const auto browser = devtools::create_database_browser(
        parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        root, {}, {}, {}, true);
    const auto list = GetDlgItem(browser, 3105), search = GetDlgItem(browser, 3101);
    const auto open = [&](const wchar_t* query) {
        SetWindowTextW(search, query);
        require(ListView_GetItemCount(list) == 1, "Asset pane fixture query is ambiguous");
        select(list, 0);
    };
    open(L"context.xmv");
    pane(browser, L"Links");
    select(GetDlgItem(browser, 3108), 0);
    press(browser, 3109);
    require(pane_name(browser) == L"Overview", "New asset type inherited another type's tab");
    pane(browser, L"Raw bytes");
    press(browser, 3110);
    require(pane_name(browser) == L"Links" && text(search) == L"context.xmv",
            "Asset Back did not restore its source tab and query");
    open(L"second.xmv");
    require(pane_name(browser) == L"Overview", "Missing asset Links tab did not fall back");
    press(browser, 3132);
    pane(browser, L"Raw bytes");
    press(browser, 3129);
    open(L"context.xmv");
    require(pane_name(browser) == L"Links", "Asset return erased its unavailable tab preference");
    open(L"context.xtx");
    pane(browser, L"Text");
    open(L"context.hot");
    require(pane_name(browser) == L"Raw bytes", "Asset Back erased the destination type's tab");
    press(browser, 3132);
    press(browser, 3129);
    open(L"second.xtx");
    require(pane_name(browser) == L"Text", "Database return replaced the text asset preference");
    DestroyWindow(browser);
}

void asset_invalidation(HWND parent, const std::filesystem::path& root) {
    devtools::NativeDatabaseSnapshot snapshot;
    snapshot.available = true;
    snapshot.manager_address = 1;
    const auto browser = devtools::create_database_browser(
        parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        root, {}, [&] { return snapshot; });
    press(browser, 3129);
    SetWindowTextW(GetDlgItem(browser, 3101), L"context.xtx");
    select(GetDlgItem(browser, 3105), 0);
    for (const bool available : {true, false, true}) {
        pane(browser, L"Text");
        if (available) {
            ++snapshot.manager_address;
        }
        snapshot.available = available;
        press(browser, 3102);
        require(pane_name(browser) == L"Overview",
                "Invalidated source retained an asset preference");
    }
    select(GetDlgItem(browser, 3127), 0);
    require(pane_name(browser) == L"Fields", "Asset invalidation damaged the database default tab");
    DestroyWindow(browser);
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      (L"xfiles-flow-" + std::to_wstring(GetCurrentProcessId()));
    HWND parent = nullptr;
    try {
        std::filesystem::create_directories(root / L"XT");
        const auto bytes = stored_list_fixture::make();
        std::ofstream(root / L"XFILES.HDB", std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::ofstream(root / L"XT/context.xtx") << "Readable context\n";
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent != nullptr, "Cannot create test host");
        check(parent, root);
        scroll_return(parent, root);
        table_panes(parent, root);
        std::ofstream(root / L"XT/context.xmv") << "Movie fixture";
        std::ofstream(root / L"XT/context.hot") << "Hotspot fixture";
        std::ofstream(root / L"XT/second.xmv") << "Other movie";
        std::ofstream(root / L"XT/second.xtx") << "Second text";
        asset_panes(parent, root);
        asset_invalidation(parent, root);
        const auto empty = root / L"empty";
        std::filesystem::create_directories(empty);
        const auto browser = devtools::create_database_browser(
            parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
            empty, {}, {}, {}, true);
        search_shortcut(parent, browser);
        require(!IsWindowEnabled(GetDlgItem(browser, 3132)) &&
                    SendMessageW(GetDlgItem(browser, 3129), BM_GETCHECK, 0, 0) == BST_CHECKED &&
                    text(GetDlgItem(browser, 3114)).starts_with(L"Asset files") &&
                    properties(browser).find(L"Header words") == std::wstring::npos,
                "Folder without a database has misleading primary navigation");
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Database and asset navigation, view restoration and filtered Back passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
