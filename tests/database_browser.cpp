#include "devtools/database/browser.h"
#include "database_fixture.h"
#include "game/database/io.h"
#include "enhancements/edition.h"
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
DatabaseFixture* native_fixture = nullptr;
}

namespace enhancements::game {
std::byte* executable_image() {
    return native_fixture ? native_fixture->image : nullptr;
}

const Edition& edition() {
    return dvd;
}
}

namespace media {
std::filesystem::path locate_file(const std::filesystem::path& path) {
    return std::filesystem::is_regular_file(path) ? path : std::filesystem::path{};
}
}

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::wstring text(HWND window) {
    std::wstring value(GetWindowTextLengthW(window) + 1, L'\0');
    value.resize(GetWindowTextW(window, value.data(), static_cast<int>(value.size())));
    return value;
}

std::wstring overview(HWND browser) {
    const auto tree = GetDlgItem(browser, 3113);
    std::wstring result = text(GetDlgItem(browser, 3114));
    const auto visit = [&](auto&& self, HTREEITEM node) -> void {
        for (; node; node = TreeView_GetNextSibling(tree, node)) {
            wchar_t buffer[8192]{};
            TVITEMW item{};
            item.mask = TVIF_TEXT;
            item.hItem = node;
            item.pszText = buffer;
            item.cchTextMax = static_cast<int>(std::size(buffer));
            TreeView_GetItem(tree, &item);
            result += L"\n" + std::wstring(buffer);
            self(self, TreeView_GetChild(tree, node));
        }
    };
    visit(visit, TreeView_GetRoot(tree));
    return result;
}

void mode(HWND browser, unsigned index) {
    const auto control = GetDlgItem(browser, 3100);
    SendMessageW(control, CB_SETCURSEL, index, 0);
    SendMessageW(browser, WM_COMMAND, MAKEWPARAM(3100, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(control));
}

std::vector<char> read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main(int argc, char** argv) {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto temporary = std::filesystem::path(temp) /
                           (L"xfiles-database-browser-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(temporary);
    const auto fixture = temporary / L"XFILES.HDB";
    std::vector<char> bytes(512);
    bytes[3] = 5;
    bytes[30] = 1;
    const std::string sample = "XV\x7f"
                               "19808.xmv";
    std::copy(sample.begin(), sample.end(), bytes.begin() + 40);
    std::ofstream(fixture, std::ios::binary)
        .write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    HWND parent = nullptr;
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        require(InitCommonControlsEx(&controls), "common controls failed");
        const auto module = GetModuleHandleW(nullptr);
        parent = CreateWindowExW(0, L"STATIC", L"Database browser check", WS_OVERLAPPEDWINDOW, 10,
                                 10, 1200, 800, nullptr, nullptr, module, nullptr);
        require(parent != nullptr, "parent creation failed");
        const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const auto root = argc > 1 ? std::filesystem::path(argv[1]) : temporary;
        const auto hdb = root / L"XFILES.HDB";
        const auto original = read(hdb);
        std::filesystem::path previewed;
        const auto browser = devtools::create_database_browser(
            parent, module, font, root, [&](const auto& path) { previewed = path; },
            [] {
                return devtools::inspect_database(enhancements::game::executable_image(),
                                                  enhancements::game::edition());
            });
        require(browser != nullptr, "browser creation failed");
        MoveWindow(browser, 20, 20, 1140, 700, TRUE);
        ShowWindow(browser, SW_SHOW);
        const auto list = GetDlgItem(browser, 3105), detail = GetDlgItem(browser, 3106);
        require(overview(browser).find(L"Native database unavailable") != std::wstring::npos,
                "unavailable native state not shown");
        DatabaseFixture native(native_game::profile_dvd_20000);
        native_fixture = &native;
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        require(ListView_GetItemCount(list) == 8, "Typed browser cache missing");
        SetWindowTextW(GetDlgItem(browser, 3101), L"VCStdAction");
        require(ListView_GetItemCount(list) == 1, "Standard action search failed");
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        require(overview(browser).find(L"Action ID 40") != std::wstring::npos,
                "Standard action fields missing");
        const auto links = GetDlgItem(browser, 3108);
        require(ListView_GetItemCount(links) == 2, "Standard action relationships missing");
        ListView_SetItemState(links, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        SendMessageW(browser, WM_COMMAND, 3109, 0);
        require(overview(browser).find(L"Cook") != std::wstring::npos,
                "Follow name reference failed");
        require(ListView_GetItemCount(links) == 3, "Incoming references missing");
        SendMessageW(browser, WM_COMMAND, 3110, 0);
        require(overview(browser).find(L"Action ID 40") != std::wstring::npos,
                "Back navigation failed");
        native.put(0x14c04, 71u);
        native.put(0x11078, 71u);
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        SetWindowTextW(GetDlgItem(browser, 3101), L"VCStdAction");
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(links, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        SendMessageW(browser, WM_COMMAND, 3109, 0);
        native.put(0x1107c, 0u);
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        SendMessageW(browser, WM_COMMAND, 3110, 0);
        require(!IsWindowEnabled(GetDlgItem(browser, 3110)), "Vanished history kept Back enabled");
        SetWindowTextW(GetDlgItem(browser, 3101), L"");
        mode(browser, 4);
        require(ListView_GetItemCount(list) == 2, "Variable filter failed");
        ListView_SetItemState(list, 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        const auto auto_refresh = GetDlgItem(browser, 3116);
        require(auto_refresh && SendMessageW(auto_refresh, BM_GETCHECK, 0, 0) == BST_UNCHECKED,
                "Auto-refresh should start unchecked");
        ShowWindow(parent, SW_SHOW);
        native.put(0x16038, std::int32_t{8});
        Sleep(2100);
        devtools::update_database_browser(browser);
        require(overview(browser).find(L"Raw value: 5") != std::wstring::npos,
                "Unchecked auto-refresh changed snapshot");
        const auto properties = GetDlgItem(browser, 3113);
        const auto field_group = TreeView_GetRoot(properties);
        TreeView_Expand(properties, field_group, TVE_COLLAPSE);
        TreeView_SelectItem(properties, field_group);
        SendMessageW(auto_refresh, BM_SETCHECK, BST_CHECKED, 0);
        Sleep(550);
        devtools::update_database_browser(browser);
        require(overview(browser).find(L"Raw value: 8") != std::wstring::npos,
                "Auto-refresh did not update copied value");
        require(!(TreeView_GetItemState(properties, TreeView_GetRoot(properties), TVIS_EXPANDED) &
                  TVIS_EXPANDED) &&
                    TreeView_GetSelection(properties) == TreeView_GetRoot(properties),
                "Auto-refresh reset property expansion or selection");
        SendMessageW(auto_refresh, BM_SETCHECK, BST_UNCHECKED, 0);
        ShowWindow(parent, SW_HIDE);
        mode(browser, 5);
        require(ListView_GetItemCount(list) == 1, "Changed variables missing");
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        require(overview(browser).find(L"Previous refresh: value 5") != std::wstring::npos,
                "Previous variable value missing");
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        require(ListView_GetItemCount(list) == 0, "Unchanged refresh retained changes");
        const auto observed =
            CreateFileW(hdb.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(observed != INVALID_HANDLE_VALUE, "Observation fixture failed");
        game_assets::observe_database_open(observed, hdb.string().c_str(), FILE_ATTRIBUTE_NORMAL);
        native.put(0x16038, std::int32_t{12});
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        require(ListView_GetItemCount(list) == 0, "Manual refresh compared across HDB reopen");
        const auto generation = game_assets::begin_database_close(observed);
        const auto closed = CloseHandle(observed);
        game_assets::observe_database_close(observed, closed, generation);
        if (argc == 1) {
            std::filesystem::create_directories(temporary / L"XV");
            std::ofstream(temporary / L"XV/19808.xmv", std::ios::binary).put('x');
            native.put(0x1102d, std::uint8_t{8});
            native.put(0x11084, native_game::DatabaseObjectEntry{80, native.address(0x17000), 0});
            native_game::PersistentObject asset{};
            asset.vtable = native.address(native.profile.asset_reference);
            asset.id = 80;
            native.put(0x17000, asset);
            native.put(native.profile.asset_reference + 8, native.address(0x21200));
            native.put(0x21200, std::array<std::uint8_t, 6>{0xb8, 0x35, 0, 0, 0, 0xc3});
            native.put(0x17028, native_game::StoryString{0, native.address(0x24200), 32, 0});
            std::array<char, 32> descriptor{};
            const std::string encoded = "\x02"
                                        "12XV\x7f"
                                        "19808.xmv";
            std::copy(encoded.begin(), encoded.end(), descriptor.begin());
            native.put(0x24200, descriptor);
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            mode(browser, 0);
            SetWindowTextW(GetDlgItem(browser, 3101), L"VCAssetRef");
            const auto filter = GetDlgItem(browser, 3111);
            SendMessageW(filter, CB_SETCURSEL, 3, 0);
            SendMessageW(browser, WM_COMMAND, MAKEWPARAM(3111, CBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(filter));
            require(ListView_GetItemCount(list) == 1, "Asset record filter failed");
            ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            require(overview(browser).find(L"XV/19808.xmv") != std::wstring::npos,
                    "Asset record path missing");
            require(!(GetWindowLongW(detail, GWL_STYLE) & WS_VISIBLE),
                    "Raw bytes cluttered initial overview");
            require(ListView_GetItemCount(links) == 1, "Asset link missing");
            SetFocus(links);
            MSG enter{};
            enter.hwnd = links;
            enter.message = WM_KEYDOWN;
            enter.wParam = VK_RETURN;
            if (!IsDialogMessageW(parent, &enter)) {
                TranslateMessage(&enter);
                DispatchMessageW(&enter);
            }
            require(SendMessageW(GetDlgItem(browser, 3100), CB_GETCURSEL, 0, 0) == 6 &&
                        overview(browser).find(L"Installed location") != std::wstring::npos,
                    "Enter did not follow asset");
            require(ListView_GetItemCount(links) == 1, "Asset reverse reference missing");
            SendMessageW(browser, WM_COMMAND, 3115, 0);
            require(previewed == std::filesystem::path(L"XV/19808.xmv"),
                    "Asset preview callback failed");
            std::ofstream(temporary / L"aaa.bmp", std::ios::binary).put('x');
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            require(overview(browser).find(L"XV/19808.xmv") != std::wstring::npos,
                    "Refresh changed selected asset after sorted insertion");
            SendMessageW(auto_refresh, BM_SETCHECK, BST_CHECKED, 0);
            ShowWindow(parent, SW_SHOW);
            Sleep(2100);
            devtools::update_database_browser(browser);
            SendMessageW(auto_refresh, BM_SETCHECK, BST_UNCHECKED, 0);
            ShowWindow(parent, SW_HIDE);
            NMHDR double_click{};
            double_click.hwndFrom = links;
            double_click.code = NM_DBLCLK;
            SendMessageW(browser, WM_NOTIFY, 3108, reinterpret_cast<LPARAM>(&double_click));
            require(SendMessageW(GetDlgItem(browser, 3100), CB_GETCURSEL, 0, 0) == 0 &&
                        overview(browser).find(L"VCAssetRef ID 80") != std::wstring::npos,
                    "Asset incoming double-click failed");
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            require(overview(browser).find(L"VCAssetRef ID 80") != std::wstring::npos,
                    "Manual refresh lost native selection after asset auto tick");
            SendMessageW(browser, WM_COMMAND, 3110, 0);
            require(SendMessageW(GetDlgItem(browser, 3100), CB_GETCURSEL, 0, 0) == 6,
                    "Cross-view Back failed");
            std::filesystem::remove(temporary / L"XV/19808.xmv");
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            require(overview(browser).find(L"Referenced, file missing") != std::wstring::npos &&
                        !IsWindowEnabled(GetDlgItem(browser, 3115)),
                    "Missing asset preview remained enabled");
            const auto before_preview = previewed;
            SendMessageW(browser, WM_COMMAND, 3115, 0);
            require(previewed == before_preview, "Missing preview dispatched callback");
            SendMessageW(filter, CB_SETCURSEL, 2, 0);
            SendMessageW(browser, WM_COMMAND, MAKEWPARAM(3111, CBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(filter));
            require(ListView_GetItemCount(list) == 1, "Missing asset filter failed");
            SendMessageW(filter, CB_SETCURSEL, 3, 0);
            SendMessageW(browser, WM_COMMAND, MAKEWPARAM(3111, CBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(filter));
            require(ListView_GetItemCount(list) == 1, "Movie asset filter failed");
            NMLISTVIEW sort{};
            sort.hdr.hwndFrom = list;
            sort.hdr.code = LVN_COLUMNCLICK;
            sort.iSubItem = 0;
            SendMessageW(browser, WM_NOTIFY, 3105, reinterpret_cast<LPARAM>(&sort));
            require(overview(browser).find(L"XV/19808.xmv") != std::wstring::npos,
                    "Sort lost selected asset");
            native.put(0x1102d, std::uint8_t{7});
        }
        native_fixture = nullptr;
        mode(browser, 0);
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        require(ListView_GetItemCount(list) == 0 && !IsWindowEnabled(GetDlgItem(browser, 3109)),
                "Unavailable snapshot retained links");
        SetWindowTextW(GetDlgItem(browser, 3101), L"");
        mode(browser, 2);
        require(ListView_GetItemCount(list) > 0, "text candidates missing");
        SetWindowTextW(GetDlgItem(browser, 3101), L"19808");
        require(ListView_GetItemCount(list) > 0, "search failed");
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        require(overview(browser).find(L"19808") != std::wstring::npos,
                "selected text not inspected");
        SetWindowTextW(GetDlgItem(browser, 3104), L"0x20");
        SendMessageW(browser, WM_COMMAND, 3103, 0);
        require(text(detail).find(L"00000020") != std::wstring::npos, "offset navigation failed");
        SetWindowTextW(GetDlgItem(browser, 3104), L"xyz");
        SendMessageW(browser, WM_COMMAND, 3103, 0);
        require(!text(GetDlgItem(browser, 3107)).empty(), "invalid offset not handled");
        SetWindowTextW(GetDlgItem(browser, 3101), L"definitely absent value");
        require(ListView_GetItemCount(list) == 0, "search did not filter");
        mode(browser, 3);
        require(ListView_GetItemCount(list) == 0, "invented native reads");
        require(read(hdb) == original, "browser changed game database");
        if (argc == 1) {
            mode(browser, 2);
            SetWindowTextW(GetDlgItem(browser, 3101), L"");
            require(ListView_GetItemCount(list) > 0, "fixture rows missing before reload");
            std::ofstream(fixture, std::ios::binary | std::ios::trunc) << "invalid";
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            require(ListView_GetItemCount(list) == 0, "failed refresh retained old rows");
            require(text(detail).find(L"unavailable") != std::wstring::npos,
                    "failed refresh retained old bytes");
        }
        if (argc > 2) {
            mode(browser, 1);
            SetWindowTextW(GetDlgItem(browser, 3101), L"Field Office");
            ShowWindow(parent, SW_SHOW);
            MSG message{};
            const auto end = GetTickCount64() + 15000;
            while (GetTickCount64() < end && IsWindow(parent)) {
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                Sleep(10);
            }
        }
        SendMessageW(GetDlgItem(browser, 3101), WM_KEYDOWN, VK_ESCAPE, 0);
        require(!IsWindow(parent), "Nested HDB Escape did not close its owner");
        parent = nullptr;
        if (argc == 1) {
            std::ofstream(fixture, std::ios::binary | std::ios::trunc) << "invalid";
            parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 10, 10, 1200, 800,
                                     nullptr, nullptr, module, nullptr);
            const auto failed =
                devtools::create_database_browser(parent, module, font, temporary, {}, [] {
                    return devtools::NativeDatabaseSnapshot{};
                });
            mode(failed, 2);
            require(ListView_GetItemCount(GetDlgItem(failed, 3105)) == 0, "stale failed-load rows");
            require(text(GetDlgItem(failed, 3106)).find(L"unavailable") != std::wstring::npos,
                    "failed-load data not invalidated");
            DestroyWindow(parent);
            parent = nullptr;
        }
        std::filesystem::remove_all(temporary);
        std::cout << "Database browser controls passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::filesystem::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
