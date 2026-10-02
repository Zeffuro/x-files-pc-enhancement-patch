#include "stored_action_browser_fixture.h"
#include "stored_asset_list_fixture.h"
#include <fstream>
#include <iostream>

using namespace stored_action_browser_fixture;

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-asset-list-browser-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"XFILES.HDB";
    HWND parent = nullptr;
    try {
        using namespace stored_list_fixture;
        std::filesystem::create_directories(root);
        auto bytes = stored_asset_list_fixture::make();
        word(bytes, 646, 1100);
        word(bytes, 650, 5);
        bytes[654] = 1;
        word(bytes, 1100, 200);
        bytes[1104] = 1;
        const auto write = [&] {
            std::ofstream(path, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        write();
        devtools::NativeDatabaseSnapshot snapshot;
        snapshot.available = true;
        devtools::NativeDatabaseObject cached;
        cached.class_id = 0x36;
        cached.id = 200;
        cached.fields = L"Contradictory cached fields";
        cached.relationships.push_back({L"Unproved member class", 0x35, 300});
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
            require(ListView_GetItemCount(rows) == 1,
                    "Expected one searchable asset-reference list");
            select(rows, 0);
        };
        query(L"Asset[3] ID: 4294967295");
        const auto fields = properties(browser);
        for (const auto expected :
             {L"Stored asset-reference list", L"Count: 5", L"Flags (raw): 195, 0",
              L"ID-resource mark: 0x00000400", L"Resource class: 0x0000000b",
              L"Resource type (raw): 0x6e756c6c", L"Descriptor word +22 (raw): 1",
              L"Needs-release byte (raw): 255", L"Duplicate byte (raw): 0", L"Resource ID (raw): 0",
              L"Decoded descriptor: 32 bytes, stored version 1", L"Resource nodes: 1",
              L"Decoded resource bytes: 28", L"Asset[0] ID: 300", L"Asset[1] ID: 0",
              L"Asset[2] ID: 300", L"Asset[3] ID: 4294967295", L"Asset[4] ID: 301"}) {
            require(fields.find(expected) != std::wstring::npos, "Stored list field missing");
        }
        require(fields.find(L"Contradictory") == std::wstring::npos &&
                    ListView_GetItemCount(links) == 0 &&
                    !IsWindowEnabled(GetDlgItem(browser, 3109)),
                "Unproved member class or cached relationship used for stored navigation");
        for (const auto value : {L"0xffffffff", L"Raw needs-release byte 255",
                                 L"Raw duplicate byte 0", L"5 Asset-reference IDs"}) {
            query(value);
        }
        auto raw = text(GetDlgItem(browser, 3106));
        require(raw.find(L"Stored asset-reference list descriptor at 0x00000200") !=
                        std::wstring::npos &&
                    raw.find(L"Stored asset-reference resource node at 0x00000400") !=
                        std::wstring::npos &&
                    raw.find(L"ff ff ff ff 00 00 01 2d") != std::wstring::npos,
                "Full descriptor and resource bytes unavailable");
        choose(browser, 3111, 5);
        query(L"300");
        require(properties(browser).find(L"Asset-list ID: 200") != std::wstring::npos,
                "Asset action example unavailable");
        select(links, 0);
        require(IsWindowEnabled(GetDlgItem(browser, 3109)), "Asset action candidate disabled");
        SendMessageW(browser, WM_COMMAND, 3109, 0);
        require(properties(browser).find(L"Asset[4] ID: 301") != std::wstring::npos,
                "Asset candidate did not reach decoded asset-reference list");
        SendMessageW(browser, WM_COMMAND, 3110, 0);
        require(properties(browser).find(L"Asset-list ID: 200") != std::wstring::npos &&
                    captures == 1,
                "Follow/Back recaptured native state or lost action");
        choose(browser, 3111, 0);
        query(L"Asset[3] ID: 4294967295");
        word(bytes, 518, 4);
        write();
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        SetWindowTextW(GetDlgItem(browser, 3101), L"200");
        select(rows, 0);
        require(properties(browser).find(L"decoding unavailable or unsupported") !=
                        std::wstring::npos &&
                    properties(browser).find(L"Asset[0]") == std::wstring::npos && captures == 2,
                "Malformed refresh retained stale typed fields");
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Stored asset-reference list browser checks passed\n";
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
