#include "stored_action_browser_fixture.h"
#include <fstream>
#include <iostream>

using namespace stored_action_browser_fixture;

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-fields-browser-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"XFILES.HDB";
    HWND parent = nullptr;
    try {
        using namespace stored_list_fixture;
        std::filesystem::create_directories(root);
        auto bytes = stored_list_fixture::make();
        const auto definition = [&](std::uint32_t cls) {
            word(bytes, 86, cls);
            word(bytes, 118, cls);
        };
        definition(0x2f);
        word(bytes, 518, 900);
        word(bytes, 522, 4);
        word(bytes, 526, 910);
        word(bytes, 530, 2);
        bytes[900] = 'n';
        bytes[901] = 0xff;
        bytes[902] = 0;
        bytes[903] = 'z';
        bytes[910] = 't';
        bytes[911] = 0;
        const auto write = [&] {
            std::ofstream(path, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        write();
        devtools::NativeDatabaseSnapshot snapshot;
        snapshot.available = true;
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
            require(ListView_GetItemCount(rows) == 1, "Expected one stored definition");
            select(rows, 0);
        };
        query(L"String[0] data: n\\xff\\x00z");
        query(L"Flags (raw): 195, 0");
        auto fields = properties(browser);
        require(fields.find(L"Stored VCName definition") != std::wstring::npos &&
                    fields.find(L"String[1] data (escaped): t\\x00") != std::wstring::npos &&
                    fields.find(L"Decoded descriptor: 22 bytes") != std::wstring::npos &&
                    ListView_GetItemCount(links) == 0,
                "Name fields or boundaries lost");
        const auto raw = text(GetDlgItem(browser, 3106));
        require(raw.find(L"6e ff 00 7a") != std::wstring::npos &&
                    raw.find(L"Stored String[1] data") != std::wstring::npos,
                "Both complete name resources unavailable");
        definition(0x33);
        word(bytes, 518, 0xffffffff);
        word(bytes, 522, 0x80000000);
        word(bytes, 526, 0);
        write();
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        query(L"Raw field +6: 4294967295");
        fields = properties(browser);
        require(fields.find(L"Stored VCNav definition") != std::wstring::npos &&
                    fields.find(L"Field +10 (raw, 4 bytes): 2147483648") != std::wstring::npos &&
                    fields.find(L"String[") == std::wstring::npos &&
                    ListView_GetItemCount(links) == 0,
                "Navigation width or unknown link boundary lost");
        definition(0x46);
        word(bytes, 518, 300);
        bytes[522] = 255;
        bytes[523] = 0x41;
        write();
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        query(L"200 VCEnabled");
        fields = properties(browser);
        require(fields.find(L"Target ID: 300") != std::wstring::npos &&
                    fields.find(L"Target query class: 0x00000041") != std::wstring::npos &&
                    fields.find(L"Field +10 (raw, 1 bytes): 255") != std::wstring::npos &&
                    ListView_GetItemCount(links) == 1,
                "Enabled target fields unavailable");
        select(links, 0);
        require(IsWindowEnabled(GetDlgItem(browser, 3109)), "Unique enabled target unavailable");
        SendMessageW(browser, WM_COMMAND, 3109, 0);
        require(properties(browser).find(L"ID: 300") != std::wstring::npos,
                "Follow lost target identity");
        SendMessageW(browser, WM_COMMAND, 3110, 0);
        require(properties(browser).find(L"Target ID: 300") != std::wstring::npos && captures == 3,
                "Follow/Back captured native state");
        word(bytes, 518, 0xffffffff);
        write();
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        query(L"200 VCEnabled");
        select(links, 0);
        require(!IsWindowEnabled(GetDlgItem(browser, 3109)), "Missing enabled target enabled");
        word(bytes, 514, 2);
        write();
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        query(L"200 VCEnabled");
        require(properties(browser).find(L"decoding unavailable or unsupported") !=
                        std::wstring::npos &&
                    ListView_GetItemCount(links) == 0,
                "Invalid enabled refresh retained target");
        const std::pair<std::uint32_t, unsigned> fixed[] = {
            {0x2b, 30}, {0x2c, 10}, {0x37, 22}, {0x39, 22}, {0x3c, 11}, {0x3d, 18},
            {0x3e, 18}, {0x40, 18}, {0x47, 34}, {0x48, 34}, {0x49, 34}, {0x4a, 34},
            {0x4b, 34}, {0x4c, 14}, {0x4f, 30}, {0x50, 14}};
        for (const auto& [cls, extent] : fixed) {
            definition(cls);
            word(bytes, 514, 1);
            std::fill(bytes.begin() + 518, bytes.begin() + 512 + extent, 255);
            if (cls == 0x50) {
                word(bytes, 518, 900);
                word(bytes, 522, 4);
            }
            write();
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            query(L"Flags (raw): 195, 0");
            require(properties(browser).find(L"Decoded descriptor: " + std::to_wstring(extent) +
                                             L" bytes") != std::wstring::npos &&
                        ListView_GetItemCount(links) == 0,
                    "Fixed-family browser extent or unproved link boundary changed");
            if (cls == 0x37 || cls == 0x39 || cls == 0x4f) {
                query(L"Field +10 signed 16-bit projection: -1");
                require(properties(browser).find(L"signed 16-bit projection: -1") !=
                            std::wstring::npos,
                        "Signed geometry projection unavailable");
            }
            if (cls == 0x4f) {
                query(L"Field +28 signed 16-bit projection: -1");
                require(properties(browser).find(L"Field +26 signed 16-bit") == std::wstring::npos,
                        "Unsigned interface WORD mislabeled signed");
            }
        }
        const std::pair<std::uint32_t, unsigned> remaining[] = {
            {0x27, 72}, {0x28, 40},  {0x29, 45}, {0x2a, 44}, {0x2d, 38}, {0x31, 31}, {0x54, 18},
            {0x56, 14}, {0x57, 132}, {0x58, 14}, {0x59, 32}, {0x5a, 14}, {0x5b, 14}, {0x5c, 14}};
        for (const auto& [cls, extent] : remaining) {
            bytes = stored_list_fixture::make();
            bytes.resize(2048);
            definition(cls);
            word(bytes, 296, 900);
            bytes[900] = 0xc3;
            word(bytes, 902, 1);
            if (cls >= 0x27 && cls <= 0x2a) {
                std::copy_n(bytes.begin() + 512, 32, bytes.begin() + 900);
            }
            if (cls == 0x56 || cls == 0x58 || cls == 0x59 || cls >= 0x5a) {
                word(bytes, 906, 1500);
                word(bytes, 910, 5);
                bytes[1500] = 0x81;
                bytes[1501] = 0x82;
                bytes[1502] = 0x83;
                bytes[1503] = 0x84;
                bytes[1504] = 0xff;
            }
            if (cls == 0x57) {
                for (unsigned i = 0; i < 5; ++i) {
                    word(bytes, 966 + 8 * i, 1500 + i * 16);
                    word(bytes, 970 + 8 * i, 5);
                }
                bytes[1030] = 0xff;
                bytes[1031] = 0xff;
            }
            write();
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            query(L"Flags (raw): 195, 0");
            fields = properties(browser);
            require(fields.find(L"Decoded descriptor: " + std::to_wstring(extent) + L" bytes") !=
                            std::wstring::npos &&
                        ListView_GetItemCount(links) == 0,
                    "Remaining class extent or unproved link boundary changed");
            if (cls >= 0x27 && cls <= 0x2a) {
                query(L"Member[3] ID: 4294967295");
                require(properties(browser).find(L"Member[4] ID: 301") != std::wstring::npos &&
                            text(GetDlgItem(browser, 3106))
                                    .find(L"Stored reference-list resource node") !=
                                std::wstring::npos,
                        "Scene list UI lost ordering or complete raw nodes");
            }
            if (cls == 0x56 || cls == 0x58 || cls >= 0x5a) {
                query(L"LE32[0] raw word: 2223211137");
                require(properties(browser).find(L"Trailing raw bytes: 1") != std::wstring::npos &&
                            text(GetDlgItem(browser, 3106)).find(L"81 82 83 84 ff") !=
                                std::wstring::npos,
                        "LE32 projection dropped raw tail");
            }
            if (cls == 0x57) {
                query(L"Field +130 signed 16-bit projection: -1");
                require(fields.find(L"Raw little-endian words") == std::wstring::npos &&
                            fields.find(L"Resource[4] size: 5") != std::wstring::npos,
                        "Opaque state blob mislabeled or omitted");
            }
            if (cls == 0x27) {
                word(bytes, 932, 1024);
                word(bytes, 936, 4);
                write();
                SendMessageW(browser, WM_COMMAND, 3102, 0);
                query(L"200 VCTitle");
                require(properties(browser).find(L"decoding unavailable or unsupported") !=
                                std::wstring::npos &&
                            properties(browser).find(L"Member[") == std::wstring::npos &&
                            text(GetDlgItem(browser, 3106))
                                    .find(L"Stored reference-list resource node") ==
                                std::wstring::npos,
                        "Invalid composite refresh retained partial decoded list");
            }
        }
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Stored field browser checks passed\n";
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
