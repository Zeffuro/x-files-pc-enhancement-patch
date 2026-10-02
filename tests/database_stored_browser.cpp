#include "devtools/database/browser.h"
#include <commctrl.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void word(std::vector<unsigned char>& bytes, unsigned at, unsigned value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<unsigned char>(value >> (24 - 8 * i));
    }
}

void node(std::vector<unsigned char>& bytes, unsigned at, bool leaf, unsigned count) {
    bytes[at] = leaf ? 0x8a : 0x4b;
    bytes[at + 1] = 0x31;
    word(bytes, at + 2, 1);
    bytes[at + 6] = static_cast<unsigned char>(count >> 8);
    bytes[at + 7] = static_cast<unsigned char>(count);
}

std::wstring text(HWND window) {
    std::wstring result(GetWindowTextLengthW(window) + 1, L'\0');
    result.resize(GetWindowTextW(window, result.data(), static_cast<int>(result.size())));
    return result;
}

std::wstring properties(HWND browser) {
    const auto tree = GetDlgItem(browser, 3113);
    std::vector<HTREEITEM> pending{TreeView_GetRoot(tree)};
    std::wstring result;
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!current) {
            continue;
        }
        wchar_t label[1024]{};
        TVITEMW item{};
        item.mask = TVIF_TEXT;
        item.hItem = current;
        item.pszText = label;
        item.cchTextMax = 1024;
        if (TreeView_GetItem(tree, &item)) {
            result += std::wstring(label) + L"\n";
        }
        pending.push_back(TreeView_GetNextSibling(tree, current));
        pending.push_back(TreeView_GetChild(tree, current));
    }
    return result;
}

void select(HWND browser, int row) {
    ListView_SetItemState(GetDlgItem(browser, 3105), row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
}

void mode(HWND browser, unsigned value) {
    SendMessageW(GetDlgItem(browser, 3100), CB_SETCURSEL, value, 0);
    SendMessageW(browser, WM_COMMAND, MAKEWPARAM(3100, CBN_SELCHANGE), 0);
}
}

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-stored-browser-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"XFILES.HDB";
    HWND parent = nullptr;
    try {
        std::filesystem::create_directories(root);
        std::vector<unsigned char> bytes(512);
        word(bytes, 0, 5);
        word(bytes, 8, 32);
        word(bytes, 20, 0x501);
        word(bytes, 24, 0x40000);
        word(bytes, 28, 256);
        node(bytes, 32, false, 1);
        word(bytes, 40, 64);
        node(bytes, 64, true, 1);
        bytes[82] = 1;
        word(bytes, 83, 0x2f);
        word(bytes, 99, 7);
        word(bytes, 103, 128);
        word(bytes, 107, 0x2f);
        word(bytes, 111, 0x6e756c6c);
        node(bytes, 128, false, 1);
        word(bytes, 136, 160);
        node(bytes, 160, true, 2);
        word(bytes, 168, 256);
        word(bytes, 172, 298);
        word(bytes, 176, 272);
        word(bytes, 180, 300);
        node(bytes, 256, true, 0);
        node(bytes, 272, true, 0);
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        std::filesystem::create_directory(root / L"XV");
        const unsigned char hotspot[] = {'H', 'S', 'P', 'T', 1,  0, 0,  0, 2, 0, 0,  0, 10, 0,
                                         20,  0,   30,  0,   40, 0, 42, 0, 0, 0, 43, 0, 0,  0};
        std::ofstream(root / L"XV/7.HOT", std::ios::binary)
            .write(reinterpret_cast<const char*>(hotspot), sizeof(hotspot));
        std::ofstream(root / L"XV/7.XMV", std::ios::binary).put('x');
        const auto module = GetModuleHandleW(nullptr);
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800, nullptr,
                                 nullptr, module, nullptr);
        require(parent != nullptr, "Cannot create test host");
        const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const auto standalone =
            devtools::create_database_browser(parent, module, font, root, {}, {}, path);
        const auto modes = GetDlgItem(standalone, 3100);
        const auto selected_mode = SendMessageW(modes, CB_GETCURSEL, 0, 0);
        require(SendMessageW(modes, CB_GETITEMDATA, selected_mode, 0) == 7,
                "Offline host did not start in Stored records");
        require(SendMessageW(modes, CB_GETCOUNT, 0, 0) == 4,
                "Offline host exposed live-game modes");
        require(text(GetDlgItem(standalone, 3107)).find(L"native") == std::wstring::npos,
                "Offline status exposed native observations");
        require(!(GetWindowLongW(GetDlgItem(standalone, 3116), GWL_STYLE) & WS_VISIBLE),
                "Offline host showed live auto-refresh");
        require(!IsWindowEnabled(GetDlgItem(standalone, 3116)),
                "Offline auto-refresh enabled without provider");
        require(ListView_GetItemCount(GetDlgItem(standalone, 3105)) == 2,
                "Offline records unavailable");
        select(standalone, 1);
        require(text(GetDlgItem(standalone, 3114)).find(L"ID 300") != std::wstring::npos,
                "Uncached selection unavailable");
        require(text(GetDlgItem(standalone, 3106)).find(L"00000110") != std::wstring::npos,
                "Uncached bytes missing");
        SendMessageW(standalone, WM_COMMAND, 3102, 0);
        require(text(GetDlgItem(standalone, 3114)).find(L"ID 300") != std::wstring::npos,
                "Offline refresh lost selection");
        mode(standalone, 2);
        SetWindowTextW(GetDlgItem(standalone, 3101), L"7.HOT");
        require(ListView_GetItemCount(GetDlgItem(standalone, 3105)) == 1,
                "Offline compact Assets mode did not map correctly");
        select(standalone, 0);
        const auto raw = GetDlgItem(standalone, 3106);
        const auto hot_bytes = text(raw);
        require(hot_bytes.find(L"48 53 50 54") != std::wstring::npos, "HOT raw bytes missing");
        require(!IsWindowEnabled(GetDlgItem(standalone, 3103)) &&
                    !IsWindowEnabled(GetDlgItem(standalone, 3104)),
                "HOT selection exposed HDB offset controls");
        SetWindowTextW(GetDlgItem(standalone, 3104), L"8");
        SendMessageW(standalone, WM_COMMAND, 3103, 0);
        require(text(raw) == hot_bytes, "Forged HOT jump replaced bytes with HDB content");
        const auto offline_links = GetDlgItem(standalone, 3108);
        require(ListView_GetItemCount(offline_links) == 1, "HOT sibling link missing");
        ListView_SetItemState(offline_links, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        SendMessageW(standalone, WM_COMMAND, 3109, 0);
        require(SendMessageW(modes, CB_GETCURSEL, 0, 0) == 2 &&
                    text(GetDlgItem(standalone, 3114)).find(L"7.XMV") != std::wstring::npos,
                "Offline Follow lost compact Assets mapping");
        SendMessageW(standalone, WM_COMMAND, 3110, 0);
        require(text(raw) == hot_bytes && SendMessageW(modes, CB_GETCURSEL, 0, 0) == 2,
                "Offline Back did not restore HOT selection");
        mode(standalone, 3);
        SetWindowTextW(GetDlgItem(standalone, 3101), L"");
        require(ListView_GetItemCount(GetDlgItem(standalone, 3105)) == 2,
                "Offline compact Stored records mode did not map correctly");
        DestroyWindow(standalone);
        devtools::NativeDatabaseSnapshot snapshot;
        snapshot.available = true;
        devtools::NativeDatabaseObject cached;
        cached.class_id = 0x2f;
        cached.id = 298;
        cached.relationships.push_back({L"Name", 0x2f, 300});
        snapshot.objects.push_back(cached);
        unsigned captures = 0;
        const auto live = devtools::create_database_browser(
            parent, module, font, root, {},
            [&] {
                ++captures;
                return snapshot;
            },
            path);
        select(live, 0);
        const auto links = GetDlgItem(live, 3108);
        require(ListView_GetItemCount(links) == 2, "Stored target and definition links missing");
        SendMessageW(live, WM_COMMAND, 3109, 0);
        require(SendMessageW(GetDlgItem(live, 3100), CB_GETCURSEL, 0, 0) == 7 &&
                    text(GetDlgItem(live, 3114)).find(L"ID 300") != std::wstring::npos,
                "Uncached Follow failed");
        require(captures == 1, "Stored Follow called snapshot provider");
        SendMessageW(live, WM_COMMAND, 3110, 0);
        require(text(GetDlgItem(live, 3114)).find(L"ID 298") != std::wstring::npos,
                "Stored Back failed");
        snapshot.objects.push_back(cached);
        snapshot.objects.back().id = 300;
        snapshot.objects.push_back(snapshot.objects.back());
        SendMessageW(live, WM_COMMAND, 3102, 0);
        select(live, 0);
        require(!IsWindowEnabled(GetDlgItem(live, 3109)),
                "Ambiguous cached link fell back to stored record");
        mode(live, 7);
        select(live, 0);
        require(ListView_GetItemCount(GetDlgItem(live, 3105)) == 2,
                "Cached ambiguity hid file rows");
        bytes[86] = 0x53;
        word(bytes, 107, 0x53);
        node(bytes, 160, true, 1);
        word(bytes, 262, 384);
        word(bytes, 266, 5);
        word(bytes, 270, 0xfffffffe);
        word(bytes, 274, 0x12b);
        bytes[278] = 0x27;
        bytes[279] = 0x81;
        std::copy_n("iTes", 5, bytes.begin() + 384);
        const auto gam_path = root / L"XFILES.GAM";
        std::ofstream(gam_path, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        const auto gam =
            devtools::create_database_browser(parent, module, font, root, {}, {}, gam_path);
        SetWindowTextW(GetDlgItem(gam, 3101), L"iTes");
        require(ListView_GetItemCount(GetDlgItem(gam, 3105)) == 1,
                "GAM variable name was not searchable");
        select(gam, 0);
        const auto fields = properties(gam);
        require(fields.find(L"Name: iTes") != std::wstring::npos &&
                    fields.find(L"Value (signed 32-bit): -2") != std::wstring::npos &&
                    fields.find(L"Value bits: 0xfffffffe") != std::wstring::npos &&
                    fields.find(L"Type (raw): 129") != std::wstring::npos,
                "GAM browser did not expose verified variable fields");
        require(ListView_GetItemCount(GetDlgItem(gam, 3108)) == 0,
                "GAM source inherited native cached links");
        DestroyWindow(gam);
        std::fill(bytes.begin(), bytes.end(), 0);
        word(bytes, 0, 5);
        word(bytes, 8, 64);
        word(bytes, 20, 0x501);
        word(bytes, 24, 0x40000);
        word(bytes, 28, 256);
        node(bytes, 64, true, 2);
        bytes[82] = 1;
        word(bytes, 83, 0x51);
        word(bytes, 99, 7);
        word(bytes, 103, 160);
        word(bytes, 107, 0x51);
        bytes[125] = 1;
        word(bytes, 126, 0x42);
        word(bytes, 142, 7);
        word(bytes, 146, 192);
        word(bytes, 150, 0x42);
        node(bytes, 160, true, 1);
        word(bytes, 168, 256);
        word(bytes, 172, 100);
        node(bytes, 192, true, 1);
        word(bytes, 200, 288);
        word(bytes, 204, 200);
        node(bytes, 256, true, 0);
        word(bytes, 262, 200);
        bytes[266] = 8;
        node(bytes, 288, true, 0);
        const auto write_trigger = [&] {
            std::ofstream(path, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
        };
        write_trigger();
        snapshot.objects.clear();
        cached.class_id = 0x42;
        cached.id = 200;
        cached.relationships.clear();
        snapshot.objects = {cached, cached};
        captures = 0;
        const auto trigger_browser = devtools::create_database_browser(
            parent, module, font, root, {},
            [&] {
                ++captures;
                return snapshot;
            },
            path);
        mode(trigger_browser, 7);
        SetWindowTextW(GetDlgItem(trigger_browser, 3101), L"Object Activation");
        require(ListView_GetItemCount(GetDlgItem(trigger_browser, 3105)) == 1,
                "Stored trigger fields not searchable");
        select(trigger_browser, 0);
        auto trigger_fields = properties(trigger_browser);
        require(trigger_fields.find(L"Action-list ID: 200") != std::wstring::npos &&
                    trigger_fields.find(L"Event type (raw): 8") != std::wstring::npos &&
                    trigger_fields.find(L"Event: Object Activation") != std::wstring::npos &&
                    trigger_fields.find(L"Runtime parameters") == std::wstring::npos,
                "Stored trigger fields mixed with runtime parameters");
        require(IsWindowEnabled(GetDlgItem(trigger_browser, 3109)),
                "Static stored link was blocked by unrelated cached ambiguity");
        SendMessageW(trigger_browser, WM_COMMAND, 3109, 0);
        require(text(GetDlgItem(trigger_browser, 3114)).find(L"VCActionList ID 200") !=
                        std::wstring::npos &&
                    SendMessageW(GetDlgItem(trigger_browser, 3100), CB_GETCURSEL, 0, 0) == 7,
                "Stored trigger Follow did not stay in stored definitions");
        SendMessageW(trigger_browser, WM_COMMAND, 3110, 0);
        require(properties(trigger_browser).find(L"Action-list ID: 200") != std::wstring::npos &&
                    captures == 1,
                "Stored trigger Follow/Back refreshed native objects");
        SetWindowTextW(GetDlgItem(trigger_browser, 3101), L"100");
        for (unsigned failure = 0; failure < 4; ++failure) {
            word(bytes, 262, failure == 0 ? 0 : failure == 1 ? 201 : 200);
            bytes[266] = 255;
            node(bytes, 192, true, failure == 2 ? 2 : 1);
            if (failure == 2) {
                word(bytes, 208, 304);
                word(bytes, 212, 200);
                node(bytes, 304, true, 0);
            }
            word(bytes, 258, failure == 3 ? 2 : 1);
            write_trigger();
            SendMessageW(trigger_browser, WM_COMMAND, 3102, 0);
            select(trigger_browser, 0);
            trigger_fields = properties(trigger_browser);
            require(!IsWindowEnabled(GetDlgItem(trigger_browser, 3109)),
                    "Missing/null/ambiguous/unsupported stored trigger enabled Follow");
            require(trigger_fields.find(L"Event: Object Activation") == std::wstring::npos,
                    "Refresh left stale or guessed trigger event label");
            require(failure != 3 ||
                        (trigger_fields.find(L"decoding unavailable") != std::wstring::npos &&
                         trigger_fields.find(L"Action-list ID:") == std::wstring::npos),
                    "Unsupported trigger refresh retained decoded fields");
        }
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Stored browser controls passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
