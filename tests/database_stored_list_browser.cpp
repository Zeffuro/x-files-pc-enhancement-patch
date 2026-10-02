#include "stored_list_fixture.h"
#include "devtools/database/browser.h"
#include <windows.h>
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

std::wstring text(HWND window) {
    std::wstring result(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    GetWindowTextW(window, result.data(), static_cast<int>(result.size()));
    result.resize(wcslen(result.c_str()));
    return result;
}

std::wstring properties(HWND browser) {
    const auto tree = GetDlgItem(browser, 3113);
    std::vector<HTREEITEM> pending{TreeView_GetRoot(tree)};
    std::wstring result;
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        if (!node) {
            continue;
        }
        wchar_t label[4096]{};
        TVITEMW item{};
        item.mask = TVIF_TEXT;
        item.hItem = node;
        item.pszText = label;
        item.cchTextMax = 4096;
        TreeView_GetItem(tree, &item);
        result += std::wstring(label) + L"\n";
        pending.push_back(TreeView_GetNextSibling(tree, node));
        pending.push_back(TreeView_GetChild(tree, node));
    }
    return result;
}

void choose(HWND browser, int control, unsigned value) {
    SendMessageW(GetDlgItem(browser, control), CB_SETCURSEL, value, 0);
    SendMessageW(browser, WM_COMMAND, MAKEWPARAM(control, CBN_SELCHANGE), 0);
}

void select(HWND list, int row) {
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}
}

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-list-browser-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"XFILES.HDB";
    HWND parent = nullptr;
    try {
        using namespace stored_list_fixture;
        std::filesystem::create_directories(root);
        auto bytes = make();
        const auto write = [&] {
            std::ofstream(path, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        write();
        auto snapshot = devtools::NativeDatabaseSnapshot{};
        snapshot.available = true;
        devtools::NativeDatabaseObject cached;
        cached.class_id = 0x42;
        cached.id = 200;
        cached.fields = L"Contradictory cached fields";
        cached.relationships.push_back({L"Cached-only action", 0x41, 999});
        snapshot.objects.push_back(cached);
        cached.class_id = 0x41;
        cached.id = 300;
        snapshot.objects.push_back(cached);
        snapshot.objects.push_back(cached);
        unsigned captures = 0;
        const auto module = GetModuleHandleW(nullptr);
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800, nullptr,
                                 nullptr, module, nullptr);
        require(parent != nullptr, "Cannot create test host");
        const auto browser = devtools::create_database_browser(
            parent, module, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), root, {},
            [&] {
                ++captures;
                return snapshot;
            },
            path);
        choose(browser, 3100, 7);
        choose(browser, 3111, 6);
        SetWindowTextW(GetDlgItem(browser, 3101), L"4294967295");
        const auto rows = GetDlgItem(browser, 3105);
        require(ListView_GetItemCount(rows) == 1, "Stored member ID was not searchable");
        select(rows, 0);
        auto fields = properties(browser);
        require(fields.find(L"Count: 5") != std::wstring::npos &&
                    fields.find(L"Action[0] ID: 300") != std::wstring::npos &&
                    fields.find(L"Action[1] ID: 0") != std::wstring::npos &&
                    fields.find(L"Action[2] ID: 300") != std::wstring::npos &&
                    fields.find(L"Action[3] ID: 4294967295") != std::wstring::npos &&
                    fields.find(L"Action[4] ID: 301") != std::wstring::npos &&
                    fields.find(L"Contradictory") == std::wstring::npos &&
                    fields.find(L"Resource nodes: 1") != std::wstring::npos &&
                    fields.find(L"Decoded resource bytes: 28") != std::wstring::npos,
                "Stored list did not preserve unsigned IDs/order or mixed cached fields");
        const auto links = GetDlgItem(browser, 3108);
        require(ListView_GetItemCount(links) == 6,
                "Static ordered links/cache destination missing");
        for (int row = 0; row < 5; ++row) {
            wchar_t label[128]{};
            ListView_GetItemText(links, row, 1, label, 128);
            require(std::wstring(label) == L"Stored Action[" + std::to_wstring(row) + L"]",
                    "Static links reordered or duplicate/null links omitted");
            select(links, row);
            require(bool(IsWindowEnabled(GetDlgItem(browser, 3109))) == (row != 1 && row != 3),
                    "Null/missing or unrelated cached ambiguity affected stored Follow");
        }
        select(links, 0);
        SendMessageW(browser, WM_COMMAND, 3109, 0);
        require(text(GetDlgItem(browser, 3114)).find(L"VCAction ID 300") != std::wstring::npos &&
                    SendMessageW(GetDlgItem(browser, 3100), CB_GETCURSEL, 0, 0) == 7,
                "Stored list Follow left its copied file");
        SendMessageW(browser, WM_COMMAND, 3110, 0);
        require(properties(browser).find(L"Action[4] ID: 301") != std::wstring::npos &&
                    captures == 1,
                "Stored list Follow/Back called provider or lost definition");
        choose(browser, 3111, 8);
        SetWindowTextW(GetDlgItem(browser, 3101), L"Trigger IDs");
        require(ListView_GetItemCount(rows) == 1, "Stored trigger-list summary not searchable");
        select(rows, 0);
        fields = properties(browser);
        require(fields.find(L"Trigger[0] ID: 400") != std::wstring::npos &&
                    fields.find(L"Trigger[2] ID: 400") != std::wstring::npos,
                "Trigger IDs interpreted as action IDs");
        select(links, 0);
        SendMessageW(browser, WM_COMMAND, 3109, 0);
        require(properties(browser).find(L"Action-list ID: 200") != std::wstring::npos,
                "Trigger-list target did not reach stored trigger decoder");
        SendMessageW(browser, WM_COMMAND, 3110, 0);
        require(captures == 1, "Trigger-list navigation recaptured native state");
        choose(browser, 3111, 6);
        SetWindowTextW(GetDlgItem(browser, 3101), L"200");
        node(bytes, 1024, false, {800, 900});
        node(bytes, 800, true, {301, 300});
        node(bytes, 900, true, {0xffffffff, 0, 300});
        write();
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        select(rows, 0);
        require(properties(browser).find(L"Resource nodes: 3") != std::wstring::npos &&
                    properties(browser).find(L"Action[0] ID: 301") != std::wstring::npos,
                "Refresh did not replace leaf with flattened tree");
        const auto tree = bytes;
        snapshot.objects.clear();
        for (unsigned failure = 0; failure < 5; ++failure) {
            bytes = tree;
            if (failure == 0) {
                word(bytes, 514, 2);
            } else if (failure == 1) {
                word(bytes, 902, 2);
            } else if (failure == 2) {
                word(bytes, 1036, 1024);
            } else if (failure == 3) {
                word(bytes, 518, 6);
            } else {
                word(bytes, 20, 0x500);
            }
            write();
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            select(rows, 0);
            fields = properties(browser);
            require(fields.find(L"Action[0]") == std::wstring::npos &&
                        text(GetDlgItem(browser, 3117)).empty() &&
                        ListView_GetItemCount(links) == 0 &&
                        !IsWindowEnabled(GetDlgItem(browser, 3109)),
                    "Unsupported refresh retained stale fields or destinations");
            require(failure == 4 || fields.find(L"decoding unavailable") != std::wstring::npos,
                    "Unsupported list gave no explicit unavailable notice");
        }
        bytes = make();
        word(bytes, 276, 300);
        write();
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        select(rows, 0);
        select(links, 0);
        require(!IsWindowEnabled(GetDlgItem(browser, 3109)) &&
                    properties(browser).find(L"Action[0] ID: 300") != std::wstring::npos,
                "Ambiguous same-file target enabled Follow or hid raw ID");
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Stored list browser controls passed\n";
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
