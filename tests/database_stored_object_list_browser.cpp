#include "stored_action_browser_fixture.h"
#include "stored_asset_list_fixture.h"
#include <fstream>
#include <iostream>

using namespace stored_action_browser_fixture;

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-object-list-browser-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"XFILES.HDB";
    HWND parent = nullptr;
    try {
        using namespace stored_list_fixture;
        std::filesystem::create_directories(root);
        auto bytes = stored_asset_list_fixture::make();
        const auto write = [&] {
            std::ofstream(path, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        write();
        devtools::NativeDatabaseSnapshot snapshot;
        snapshot.available = true;
        const auto module = GetModuleHandleW(nullptr);
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800, nullptr,
                                 nullptr, module, nullptr);
        require(parent != nullptr, "Cannot create host");
        const auto browser = devtools::create_database_browser(
            parent, module, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), root, {},
            [&] { return snapshot; }, path);
        choose(browser, 3100, 7);
        choose(browser, 3111, 0);
        const auto rows = GetDlgItem(browser, 3105);
        const auto links = GetDlgItem(browser, 3108);
        for (const auto cls : {0x2eu, 0x32u, 0x34u, 0x38u, 0x3au, 0x3bu, 0x4du, 0x4eu, 0x55u}) {
            word(bytes, 86, cls);
            word(bytes, 118, cls);
            word(bytes, 544, 0xffffffff);
            bytes[548] = 255;
            write();
            SetWindowTextW(GetDlgItem(browser, 3101), L"");
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            SetWindowTextW(GetDlgItem(browser, 3101), L"Member[3] ID: 4294967295");
            require(ListView_GetItemCount(rows) == 1, "Generic ordered IDs unsearchable");
            select(rows, 0);
            const auto fields = properties(browser);
            require(fields.find(L"Count: 5") != std::wstring::npos &&
                        fields.find(L"Member[1] ID: 0") != std::wstring::npos &&
                        ListView_GetItemCount(links) == 0,
                    "Generic member IDs or unproved links changed");
            require(text(GetDlgItem(browser, 3106)).find(L"ff ff ff ff 00 00 01 2d") !=
                        std::wstring::npos,
                    "Generic complete node bytes unavailable");
            if (cls == 0x4e) {
                require(fields.find(L"Word +32 (raw): 4294967295") != std::wstring::npos &&
                            fields.find(L"Byte +36 (raw): 255") != std::wstring::npos,
                        "Extended layout fields unavailable");
                SetWindowTextW(GetDlgItem(browser, 3101), L"Raw byte +36: 255");
                require(ListView_GetItemCount(rows) == 1, "Extended field unsearchable");
            }
        }
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Stored object-list browser checks passed\n";
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
