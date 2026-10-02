#include "stored_action_browser_fixture.h"
#include "devtools/database/field_guide.h"
#include "devtools/database/model.h"
#include "devtools/database/source.h"
#include <fstream>
#include <iostream>
#include <set>

using namespace stored_action_browser_fixture;

namespace {
int tab(HWND browser, const wchar_t* caption) {
    const auto tabs = GetDlgItem(browser, 3112);
    for (int index = 0; index < TabCtrl_GetItemCount(tabs); ++index) {
        wchar_t label[64]{};
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = label;
        item.cchTextMax = 64;
        TabCtrl_GetItem(tabs, index, &item);
        if (wcscmp(label, caption) == 0) {
            return index;
        }
    }
    return -1;
}

std::wstring table(HWND list) {
    std::wstring result;
    for (int row = 0; row < ListView_GetItemCount(list); ++row) {
        for (int column = 0; column < 4; ++column) {
            wchar_t value[4096]{};
            ListView_GetItemText(list, row, column, value, 4096);
            result += std::wstring(value) + L"\t";
        }
        result += L"\n";
    }
    return result;
}

void show_tab(HWND browser, const wchar_t* caption) {
    const int index = tab(browser, caption);
    require(index >= 0, "Requested tab unavailable");
    TabCtrl_SetCurSel(GetDlgItem(browser, 3112), index);
    NMHDR notification{GetDlgItem(browser, 3112), 3112, TCN_SELCHANGE};
    SendMessageW(browser, WM_NOTIFY, 3112, reinterpret_cast<LPARAM>(&notification));
}

int installed(const std::filesystem::path& path) {
    const auto source = devtools::browser_source(path);
    const auto parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800,
                                        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(parent != nullptr, "Cannot create installed browser host");
    try {
        const auto browser = devtools::create_database_browser(
            parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
            source.root, {}, {}, source.database);
        MoveWindow(browser, 0, 0, 1180, 780, TRUE);
        const auto classes = GetDlgItem(browser, 3127), list = GetDlgItem(browser, 3105);
        const int count = ListView_GetItemCount(classes);
        require(count > 0, "Installed database class inventory empty");
        std::wcout << L"FILE\t" << path.wstring() << L"\t" << count << L"\n";
        for (int index = 0; index < count; ++index) {
            wchar_t cls[64]{}, total[64]{};
            ListView_GetItemText(classes, index, 0, cls, 64);
            ListView_GetItemText(classes, index, 2, total, 64);
            select(classes, index);
            MSG key{};
            key.hwnd = classes;
            key.message = WM_KEYDOWN;
            key.wParam = VK_RETURN;
            if (!IsDialogMessageW(parent, &key)) {
                TranslateMessage(&key);
                DispatchMessageW(&key);
            }
            require(ListView_GetItemCount(list) == std::stoi(total),
                    "Installed class page count mismatch");
            require(TabCtrl_GetCurSel(GetDlgItem(browser, 3112)) == tab(browser, L"Fields"),
                    "Installed class Enter did not open Fields");
            require(ListView_GetItemCount(GetDlgItem(browser, 3123)) > 1,
                    "Installed class has no field table");
            const auto content = table(GetDlgItem(browser, 3123));
            require(content.find(L"Purpose unknown or not yet verified") != std::wstring::npos ||
                        content.find(L"Guide\tUsed for") != std::wstring::npos,
                    "Installed field explanations unavailable");
            std::wcout << L"CLASS\t" << cls << L"\t" << total << L"\t"
                       << ListView_GetItemCount(GetDlgItem(browser, 3123)) << L"\n";
        }
        DestroyWindow(parent);
        return 0;
    } catch (...) {
        DestroyWindow(parent);
        throw;
    }
}

void pages(HWND parent, const std::filesystem::path& root) {
    devtools::NativeDatabaseSnapshot snapshot;
    snapshot.available = true;
    for (std::uint32_t id = 1000; id < 1425; ++id) {
        devtools::NativeDatabaseObject object;
        object.id = id;
        object.class_id = 0x53;
        object.description = L"Page item " + std::to_wstring(id);
        object.fields = L"Raw value: " + std::to_wstring(id);
        snapshot.objects.push_back(std::move(object));
    }
    snapshot.objects[0].relationships.push_back({L"Last page", 0x53, 1424});
    unsigned captures = 0;
    const auto browser = devtools::create_database_browser(
        parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        root, {}, [&] {
            ++captures;
            return snapshot;
        });
    const auto list = GetDlgItem(browser, 3105);
    require(ListView_GetItemCount(list) == 425, "Virtual list lost records");
    select(list, 424);
    ListView_EnsureVisible(list, 424, FALSE);
    require(text(GetDlgItem(browser, 3114)).find(L"ID 1424") != std::wstring::npos,
            "Continuous list did not select its last record");
    select(list, 0);
    show_tab(browser, L"Links");
    select(GetDlgItem(browser, 3108), 0);
    SendMessageW(browser, WM_COMMAND, 3109, 0);
    require(ListView_GetItemCount(list) == 425 &&
                text(GetDlgItem(browser, 3114)).find(L"ID 1424") != std::wstring::npos,
            "Following a link did not find its destination on the final page");
    SendMessageW(browser, WM_COMMAND, 3110, 0);
    require(ListView_GetItemCount(list) == 425 &&
                text(GetDlgItem(browser, 3114)).find(L"ID 1000") != std::wstring::npos,
            "Back lost the source page or record identity");
    SetWindowTextW(GetDlgItem(browser, 3101), L"Page item 1424");
    require(ListView_GetItemCount(list) == 1, "Search did not cover records beyond the first page");
    select(list, 0);
    SetWindowTextW(GetDlgItem(browser, 3101), L"");
    NMLISTVIEW sort{};
    sort.hdr = {list, 3105, LVN_COLUMNCLICK};
    sort.iSubItem = 0;
    SendMessageW(browser, WM_NOTIFY, 3105, reinterpret_cast<LPARAM>(&sort));
    require(text(GetDlgItem(browser, 3114)).find(L"ID 1424") != std::wstring::npos &&
                ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0,
            "Global sorting lost the selected key or destination page");
    require(captures == 1, "Scrolling or navigation captured native state");
    DestroyWindow(browser);
}
}

int main(int argc, char** argv) {
    const auto root = std::filesystem::temp_directory_path() /
                      (L"xfiles-usability-" + std::to_wstring(GetCurrentProcessId()));
    HWND parent = nullptr;
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--installed") {
            return installed(std::filesystem::path(argv[2]));
        }
        std::filesystem::create_directories(root);
        auto bytes = stored_list_fixture::make();
        const auto path = root / L"XFILES.HDB";
        const auto write = [&] {
            std::ofstream(path, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        write();
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent != nullptr, "Cannot create host");
        unsigned opens = 0;
        const auto browser = devtools::create_database_browser(
            parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
            root, [&](const auto&) { ++opens; }, {}, {}, true);
        MoveWindow(browser, 0, 0, 1180, 780, TRUE);
        const auto list = GetDlgItem(browser, 3105);
        require(SendMessageW(GetDlgItem(browser, 3127), WM_GETDLGCODE, VK_RETURN, 0) &
                    DLGC_WANTMESSAGE,
                "Database class list does not retain Enter for dialog navigation");
        SetWindowTextW(GetDlgItem(browser, 3101), L"XFILES.HDB");
        require(ListView_GetItemCount(list) == 1, "HDB asset missing");
        select(list, 0);
        require(ListView_GetItemCount(GetDlgItem(browser, 3127)) == 4 &&
                    text(GetDlgItem(browser, 3126)).find(L"5 indexed records") !=
                        std::wstring::npos,
                "Asset selection lost persistent file overview or class counts");
        require(properties(browser).find(L"not supported by the preview") == std::wstring::npos,
                "Database asset still claims its format is unsupported");
        SendMessageW(browser, WM_COMMAND, 3115, 0);
        require(opens == 0 && ListView_GetItemCount(list) == 5 &&
                    text(GetDlgItem(browser, 3101)).empty(),
                "Opening current HDB recreated browser or retained asset query");
        SetWindowTextW(GetDlgItem(browser, 3101), L"400 VCTrigger");
        select(list, 0);
        show_tab(browser, L"Fields");
        const auto field_table = table(GetDlgItem(browser, 3123));
        require(field_table.find(L"Action-list ID\t200") != std::wstring::npos &&
                    field_table.find(L"Event type (raw)\t8") != std::wstring::npos &&
                    field_table.find(L"Object Activation") != std::wstring::npos,
                "Trigger table lost decoded fields or verified event label");
        select(GetDlgItem(browser, 3123), 0);
        require(!text(GetDlgItem(browser, 3124)).empty(), "Selected field explanation unavailable");
        const auto tabs = GetDlgItem(browser, 3112);
        require((GetWindowLongW(tabs, GWL_STYLE) & WS_CLIPSIBLINGS) &&
                    GetWindow(tabs, GW_HWNDNEXT) == nullptr,
                "Tab background can paint over detail controls");
        const auto selected_title = text(GetDlgItem(browser, 3114));
        const auto selected_explanation = text(GetDlgItem(browser, 3124));
        for (const auto width : {1180, 1800, 1000, 1180}) {
            MoveWindow(browser, 0, 0, width, 780, TRUE);
            require(text(GetDlgItem(browser, 3114)) == selected_title &&
                        text(GetDlgItem(browser, 3124)) == selected_explanation &&
                        ListView_GetNextItem(GetDlgItem(browser, 3123), -1, LVNI_SELECTED) == 0,
                    "Resize lost record or field selection");
        }
        if (argc == 2 && std::string_view(argv[1]) == "--clipboard") {
            SendMessageW(browser, WM_COMMAND, 3125, 0);
            require(OpenClipboard(browser), "Cannot inspect copied table");
            const auto data = GetClipboardData(CF_UNICODETEXT);
            const auto copied = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
            const bool valid =
                copied &&
                std::wstring_view(copied).starts_with(L"Group\tField\tValue\tMeaning\r\n") &&
                std::wstring_view(copied).find(L"Action-list ID\t200") != std::wstring_view::npos;
            if (copied) {
                GlobalUnlock(data);
            }
            CloseClipboard();
            require(valid, "Clipboard table missing header or field value");
        }
        select(GetDlgItem(browser, 3127), 2);
        require(ListView_GetItemCount(list) == 1 &&
                    table(list).find(L"VCTrigger") != std::wstring::npos &&
                    TabCtrl_GetCurSel(GetDlgItem(browser, 3112)) == tab(browser, L"Fields"),
                "Class overview did not open a filtered record and Fields tab");
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        require(ListView_GetNextItem(GetDlgItem(browser, 3127), -1, LVNI_SELECTED) == 2 &&
                    ListView_GetItemCount(list) == 1,
                "Refresh lost the visible class filter");
        SendMessageW(browser, WM_COMMAND, 3128, 0);
        require(ListView_GetNextItem(GetDlgItem(browser, 3127), -1, LVNI_SELECTED) == -1 &&
                    ListView_GetItemCount(list) == 5 &&
                    TabCtrl_GetCurSel(GetDlgItem(browser, 3112)) == tab(browser, L"Overview"),
                "All records did not restore unfiltered file overview");
        const std::uint32_t classes[] = {0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x31,
                                         0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
                                         0x3c, 0x3d, 0x3e, 0x40, 0x41, 0x42, 0x46, 0x47, 0x48, 0x49,
                                         0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53,
                                         0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c};
        std::set<std::wstring> purposes;
        const auto filter = GetDlgItem(browser, 3111);
        for (const auto cls : classes) {
            purposes.insert(devtools::database_class_purpose(cls));
            stored_list_fixture::word(bytes, 86, cls);
            stored_list_fixture::word(bytes, 118, cls);
            write();
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            LRESULT found = -1;
            for (LRESULT index = 0; index < SendMessageW(filter, CB_GETCOUNT, 0, 0); ++index) {
                if (SendMessageW(filter, CB_GETITEMDATA, index, 0) == static_cast<LRESULT>(cls)) {
                    found = index;
                    break;
                }
            }
            require(found >= 0, "Available class missing from record filter");
            choose(browser, 3111, static_cast<unsigned>(found));
            const auto query = L"200 " + std::wstring(devtools::database_class_name(cls));
            SetWindowTextW(GetDlgItem(browser, 3101), query.c_str());
            require(ListView_GetItemCount(list) == 1, "Class filter or query lost record");
            select(list, 0);
            require(tab(browser, L"Fields") >= 0 &&
                        (GetWindowLongW(GetDlgItem(browser, 3127), GWL_STYLE) & WS_VISIBLE) &&
                        ListView_GetItemCount(GetDlgItem(browser, 3123)) > 1,
                    "Class has no field table or persistent database overview");
        }
        require(purposes.size() > 30 && !purposes.contains(L""),
                "Class guides are missing or generic");
        DestroyWindow(browser);
        pages(parent, root);
        unsigned captures = 0;
        const auto live = devtools::create_database_browser(
            parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
            root, {}, [&] {
                ++captures;
                return devtools::NativeDatabaseSnapshot{};
            });
        require(captures == 1, "Unexpected initial snapshot count");
        show_tab(live, L"Overview");
        select(GetDlgItem(live, 3127), 0);
        SendMessageW(live, WM_COMMAND, 3128, 0);
        show_tab(live, L"Overview");
        require(captures == 1, "Database navigation captured native state");
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Database navigation, all 49 class filters, field tables and guides passed\n";
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
