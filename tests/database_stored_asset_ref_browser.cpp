#include "stored_action_browser_fixture.h"
#include "stored_asset_ref_fixture.h"
#include <fstream>
#include <iostream>

using namespace stored_action_browser_fixture;

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-asset-ref-browser-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"XFILES.HDB";
    HWND parent = nullptr;
    try {
        using namespace stored_list_fixture;
        std::filesystem::create_directories(root);
        auto bytes = stored_asset_ref_fixture::make();
        const auto write = [&] {
            std::ofstream(path, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        write();
        devtools::NativeDatabaseSnapshot snapshot;
        snapshot.available = true;
        devtools::NativeDatabaseObject cached;
        cached.class_id = 0x35;
        cached.id = 200;
        cached.fields = L"Contradictory cached fields";
        cached.relationships.push_back({L"Unproved word", 0x53, 300});
        snapshot.objects = {cached, cached};
        unsigned captures = 0;
        const auto module = GetModuleHandleW(nullptr);
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800, nullptr,
                                 nullptr, module, nullptr);
        require(parent != nullptr, "Cannot create host");
        const auto browser = devtools::create_database_browser(
            parent, module, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), root, {},
            [&] {
                ++captures;
                return snapshot;
            },
            path);
        choose(browser, 3100, 7);
        choose(browser, 3111, 0);
        const auto rows = GetDlgItem(browser, 3105);
        const auto links = GetDlgItem(browser, 3108);
        const auto query = [&](const wchar_t* value) {
            SetWindowTextW(GetDlgItem(browser, 3101), value);
            if (ListView_GetItemCount(rows) != 1) {
                std::wcerr << L"Failed query: " << value << L", rows: "
                           << ListView_GetItemCount(rows) << L'\n';
                throw std::runtime_error("Expected searchable asset reference");
            }
            select(rows, 0);
        };
        query(L"\\x02a\\x5c\\xff\\x00z");
        query(L"Flags (raw): 195, 0");
        const auto fields = properties(browser);
        for (const auto expected :
             {L"Stored asset reference", L"Name-data mark: 0x00000384", L"Name-data size: 6",
              L"Name data (escaped): \\x02a\\x5c\\xff\\x00z",
              L"Word +14 (raw): 4294967295 (0xffffffff)",
              L"Word +18 (raw): 2147483648 (0x80000000)", L"Byte +22 (raw): 255",
              L"Byte +23 (raw): 2", L"Decoded descriptor: 24 bytes, stored version 1"}) {
            require(fields.find(expected) != std::wstring::npos, "Stored asset field missing");
        }
        require(fields.find(L"Contradictory") == std::wstring::npos &&
                    ListView_GetItemCount(links) == 0 &&
                    !IsWindowEnabled(GetDlgItem(browser, 3109)),
                "Cached relationship used for stored reference");
        for (const auto value : {L"0xffffffff", L"Raw byte +22: 255", L"Raw byte +23: 2",
                                 L"Asset name data: 6 bytes"}) {
            query(value);
        }
        auto raw = text(GetDlgItem(browser, 3106));
        require(raw.find(L"Stored asset-reference descriptor at 0x00000200") !=
                        std::wstring::npos &&
                    raw.find(L"Stored asset name data at 0x00000384") != std::wstring::npos &&
                    raw.find(L"02 61 5c ff 00 7a") != std::wstring::npos,
                "Complete binary name bytes unavailable");
        bytes.resize(7000);
        word(bytes, 518, 1200);
        word(bytes, 522, 5000);
        std::fill(bytes.begin() + 1200, bytes.begin() + 6200, 'q');
        bytes[6199] = 'z';
        write();
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        query(L"qqqqz");
        require(properties(browser).find(L"first 256 bytes") != std::wstring::npos,
                "Long name preview unbounded");
        raw = text(GetDlgItem(browser, 3106));
        require(raw.find(L"Stored asset name data at 0x000014b0") != std::wstring::npos &&
                    raw.find(L"71 71 71 71 71 71 71 7a") != std::wstring::npos,
                "Long binary name Raw truncated");
        word(bytes, 522, 65537);
        write();
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        query(L"200 VCAssetRef");
        require(properties(browser).find(L"decoding unavailable or unsupported") !=
                        std::wstring::npos &&
                    properties(browser).find(L"Word +14") == std::wstring::npos && captures == 3,
                "Malformed refresh retained stale fields");
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Stored asset-reference browser checks passed\n";
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
