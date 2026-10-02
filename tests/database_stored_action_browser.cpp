#include "stored_action_browser_fixture.h"
#include <fstream>
#include <iostream>

using namespace stored_action_browser_fixture;

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-action-browser-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"XFILES.HDB";
    HWND parent = nullptr;
    try {
        std::filesystem::create_directories(root);
        auto bytes = fixture();
        const auto write = [&] {
            std::ofstream(path, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        write();
        devtools::NativeDatabaseSnapshot snapshot;
        snapshot.available = true;
        devtools::NativeDatabaseObject cached;
        cached.class_id = 0x53;
        cached.id = 200;
        cached.fields = L"Contradictory cached value";
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
        choose(browser, 3111, 5);
        SetWindowTextW(GetDlgItem(browser, 3101), L"305419896");
        const auto rows = GetDlgItem(browser, 3105);
        const auto links = GetDlgItem(browser, 3108);
        require(ListView_GetItemCount(rows) == 1, "BE Statement value not searchable");
        select(rows, 0);
        auto fields = properties(browser);
        require(fields.find(L"Payload byte length: 23") != std::wstring::npos &&
                    fields.find(L"Variable ID 200 != (opcode 1) -1") != std::wstring::npos &&
                    fields.find(L"Variable ID 200 = (opcode 0) 305419896") != std::wstring::npos,
                "Stored operands decoded as cached little endian");
        require(ListView_GetItemCount(links) == 2, "Predicate/Statement links missing");
        select(links, 0);
        require(IsWindowEnabled(GetDlgItem(browser, 3109)),
                "Cached ambiguity affected stored link");
        SendMessageW(browser, WM_COMMAND, 3109, 0);
        require(text(GetDlgItem(browser, 3114)).find(L"VCVariable ID 200") != std::wstring::npos &&
                    properties(browser).find(L"Name: stored") != std::wstring::npos &&
                    captures == 1,
                "Follow loaded native state or lost stored identity");
        SendMessageW(browser, WM_COMMAND, 3110, 0);
        require(properties(browser).find(L"Payload byte length: 23") != std::wstring::npos &&
                    captures == 1,
                "Back recaptured provider");
        SetWindowTextW(GetDlgItem(browser, 3101), L"300");
        bytes[821] = 4;
        stored_list_fixture::word(bytes, 816, 0xffffffff);
        write();
        SendMessageW(browser, WM_COMMAND, 3102, 0);
        select(rows, 0);
        require(properties(browser).find(L"Trigger context index 4294967295") !=
                        std::wstring::npos &&
                    ListView_GetItemCount(links) == 2,
                "Context index linked or guessed a value");
        for (unsigned target_case = 0; target_case < 5; ++target_case) {
            bytes = fixture();
            stored_list_fixture::word(bytes, 86, target_case == 1 ? 0x35 : 0x36);
            stored_list_fixture::word(bytes, 118, target_case == 1 ? 0x35 : 0x36);
            stored_list_fixture::word(bytes, 172, target_case == 2 ? 0x36 : 0x35);
            stored_list_fixture::word(bytes, 204, target_case == 2 ? 0x36 : 0x35);
            stored_list_fixture::word(bytes, 364, 200);
            stored_list_fixture::word(bytes, 650, 17);
            bytes[654] = 0x81;
            bytes[808] = 2;
            stored_list_fixture::word(bytes, 812, target_case == 3 ? 0 : 200);
            bytes[816] = target_case == 4 ? 255 : 1;
            write();
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            const auto captures_before_follow = captures;
            SetWindowTextW(GetDlgItem(browser, 3101), L"Raw asset selector");
            require(ListView_GetItemCount(rows) == 1, "Asset selector not searchable");
            select(rows, 0);
            fields = properties(browser);
            require(fields.find(L"Asset-list ID:") != std::wstring::npos &&
                        fields.find(L"Asset selector (raw):") != std::wstring::npos &&
                        fields.find(L"Asset reference class: 0x00000036") != std::wstring::npos &&
                        fields.find(L"Action body unsupported") == std::wstring::npos &&
                        ListView_GetItemCount(links) == 1,
                    "Asset fields/class or raw selector navigation lost");
            select(links, 0);
            const bool available = target_case == 0 || target_case == 4;
            require(bool(IsWindowEnabled(GetDlgItem(browser, 3109))) == available,
                    "Wrong class, duplicate or null Asset identity linked");
            if (available) {
                SendMessageW(browser, WM_COMMAND, 3109, 0);
                require(text(GetDlgItem(browser, 3114)).find(L"ID 200") != std::wstring::npos &&
                            captures == captures_before_follow,
                        "Asset Follow lost same-file identity or captured native state");
                SendMessageW(browser, WM_COMMAND, 3110, 0);
                require(properties(browser).find(L"Asset selector (raw):") != std::wstring::npos &&
                            captures == captures_before_follow,
                        "Asset Back lost static fields or recaptured native state");
            }
        }
        for (const bool conditional : {false, true}) {
            bytes = fixture();
            const unsigned prefix = conditional ? 12 : 0;
            stored_list_fixture::word(bytes, 650, prefix + 6);
            bytes[654] = conditional ? 0x82 : 2;
            bytes[808] = 2;
            stored_list_fixture::word(bytes, 800 + prefix, 0xffffffff);
            bytes[804 + prefix] = 200;
            bytes[805 + prefix] = 255;
            write();
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            SetWindowTextW(GetDlgItem(browser, 3101), L"Raw timer control");
            require(ListView_GetItemCount(rows) == 1, "Timer control not searchable");
            select(rows, 0);
            fields = properties(browser);
            require(fields.find(L"Timer duration (raw): 4294967295") != std::wstring::npos &&
                        fields.find(L"Timer ID: 200") != std::wstring::npos &&
                        fields.find(L"Timer control byte (raw): 255") != std::wstring::npos &&
                        fields.find(L"Timer selector (low 7 bits): 127") != std::wstring::npos &&
                        fields.find(L"Timer flag (bit 7): 1") != std::wstring::npos &&
                        fields.find(L"Action body unsupported") == std::wstring::npos &&
                        ListView_GetItemCount(links) == 0,
                    "Timer fields narrowed or runtime timer ID linked as a variable");
            require(text(GetDlgItem(browser, 3106)).find(L"Stored action payload") !=
                        std::wstring::npos,
                    "Timer raw payload lost");
            SetWindowTextW(GetDlgItem(browser, 3101), L"0xffffffff");
            require(ListView_GetItemCount(rows) == 1, "Timer duration hex not searchable");
            stored_list_fixture::word(bytes, 650, prefix + 7);
            write();
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            SetWindowTextW(GetDlgItem(browser, 3101), L"300");
            select(rows, 0);
            require(properties(browser).find(L"Action body unsupported") != std::wstring::npos &&
                        properties(browser).find(L"Timer duration") == std::wstring::npos &&
                        ListView_GetItemCount(links) == 0,
                    "Extended Timer body kept stale fields or links");
        }
        for (const bool conditional : {false, true}) {
            bytes = fixture();
            const unsigned prefix = conditional ? 12 : 0;
            stored_list_fixture::word(bytes, 650, prefix + 9);
            bytes[654] = conditional ? 0x83 : 3;
            stored_list_fixture::word(bytes, 800 + prefix, 200);
            stored_list_fixture::word(bytes, 804 + prefix, 0x80000001);
            bytes[808 + prefix] = 255;
            write();
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            const auto captures_before = captures;
            for (const auto query : {L"Enable target ID 200", L"Enable raw word 2147483649",
                                     L"Raw enable control 255", L"0x80000001"}) {
                SetWindowTextW(GetDlgItem(browser, 3101), query);
                require(ListView_GetItemCount(rows) == 1, "Enable fields not searchable");
            }
            select(rows, 0);
            fields = properties(browser);
            require(fields.find(L"Enable target ID: 200") != std::wstring::npos &&
                        fields.find(L"Enable word at body +4 (raw): 2147483649") !=
                            std::wstring::npos &&
                        fields.find(L"Enable control byte (raw): 255") != std::wstring::npos &&
                        fields.find(L"Enable flag (nonzero): 1") != std::wstring::npos &&
                        fields.find(L"Enable reference class: 0x00000046 (VCEnabled)") !=
                            std::wstring::npos &&
                        ListView_GetItemCount(links) == (conditional ? 1 : 0) &&
                        captures == captures_before,
                    "Enable fields narrowed, target linked or native provider recaptured");
            require(text(GetDlgItem(browser, 3106)).find(L"80 00 00 01") != std::wstring::npos,
                    "Enable unknown word raw payload lost");
            if (conditional) {
                select(links, 0);
                require(IsWindowEnabled(GetDlgItem(browser, 3109)),
                        "Enable predicate variable link lost");
                SendMessageW(browser, WM_COMMAND, 3109, 0);
                require(text(GetDlgItem(browser, 3114)).find(L"VCVariable ID 200") !=
                                std::wstring::npos &&
                            captures == captures_before,
                        "Enable predicate Follow loaded native state");
                SendMessageW(browser, WM_COMMAND, 3110, 0);
                require(properties(browser).find(L"Enable target ID: 200") != std::wstring::npos &&
                            captures == captures_before,
                        "Enable predicate Back lost static body");
            }
            stored_list_fixture::word(bytes, 650, prefix + 10);
            write();
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            SetWindowTextW(GetDlgItem(browser, 3101), L"300");
            select(rows, 0);
            require(properties(browser).find(L"Action body unsupported") != std::wstring::npos &&
                        properties(browser).find(L"Enable target ID:") == std::wstring::npos &&
                        ListView_GetItemCount(links) == (conditional ? 1 : 0),
                    "Extended Enable body retained stale fields or target links");
        }
        const std::uint32_t view_classes[] = {0x2b, 0x28, 0x29, 0x2a};
        const wchar_t* view_labels[] = {L"view", L"node", L"location", L"viewpoint"};
        for (const bool conditional : {false, true}) {
            for (unsigned field = 0; field < 4; ++field) {
                for (unsigned target_case = 0; target_case < 4; ++target_case) {
                    bytes = fixture();
                    const unsigned prefix = conditional ? 12 : 0;
                    const auto target_class = view_classes[field];
                    stored_list_fixture::word(bytes, 86, target_case == 1 ? 0x35 : target_class);
                    stored_list_fixture::word(bytes, 118, target_case == 1 ? 0x35 : target_class);
                    stored_list_fixture::word(bytes, 129, target_case == 2 ? target_class : 0x35);
                    stored_list_fixture::word(bytes, 161, target_case == 2 ? target_class : 0x35);
                    stored_list_fixture::word(bytes, 332, 200);
                    stored_list_fixture::word(bytes, 172, 0x53);
                    stored_list_fixture::word(bytes, 204, 0x53);
                    stored_list_fixture::word(bytes, 800, 201);
                    stored_list_fixture::word(bytes, 650, prefix + 16);
                    bytes[654] = conditional ? 0x84 : 4;
                    for (unsigned i = 0; i < 4; ++i) {
                        stored_list_fixture::word(bytes, 800 + prefix + i * 4,
                                                  i == field ? (target_case == 3 ? 0 : 200)
                                                             : 0x80000001);
                    }
                    write();
                    SendMessageW(browser, WM_COMMAND, 3102, 0);
                    const auto captures_before = captures;
                    SetWindowTextW(GetDlgItem(browser, 3101), L"Set View");
                    require(ListView_GetItemCount(rows) == 1, "Set View IDs not searchable");
                    select(rows, 0);
                    fields = properties(browser);
                    const auto label = L"Set View " + std::wstring(view_labels[field]) + L" ID: ";
                    require(fields.find(label + (target_case == 3 ? L"0" : L"200")) !=
                                    std::wstring::npos &&
                                fields.find(L"2147483649") != std::wstring::npos &&
                                fields.find(L"Action body unsupported") == std::wstring::npos &&
                                ListView_GetItemCount(links) == (conditional ? 5 : 4),
                            "Set View fields narrowed, reordered or candidate links lost");
                    select(links, field + (conditional ? 1 : 0));
                    require(bool(IsWindowEnabled(GetDlgItem(browser, 3109))) == (target_case == 0),
                            "Set View wrong class, duplicate, missing or null identity linked");
                    if (target_case == 0) {
                        SendMessageW(browser, WM_COMMAND, 3109, 0);
                        require(text(GetDlgItem(browser, 3114)).find(L"ID 200") !=
                                        std::wstring::npos &&
                                    captures == captures_before,
                                "Set View Follow loaded native state or lost stored identity");
                        SendMessageW(browser, WM_COMMAND, 3110, 0);
                        require(properties(browser).find(label) != std::wstring::npos &&
                                    captures == captures_before,
                                "Set View Back lost fields or recaptured native state");
                    }
                    SetWindowTextW(GetDlgItem(browser, 3101), L"0x80000001");
                    require(ListView_GetItemCount(rows) == 1, "Set View hex IDs not searchable");
                    require(text(GetDlgItem(browser, 3106)).find(L"80 00 00 01") !=
                                std::wstring::npos,
                            "Set View complete raw payload lost");
                    stored_list_fixture::word(bytes, 650, prefix + 17);
                    write();
                    SendMessageW(browser, WM_COMMAND, 3102, 0);
                    SetWindowTextW(GetDlgItem(browser, 3101), L"300");
                    select(rows, 0);
                    require(properties(browser).find(L"Action body unsupported") !=
                                    std::wstring::npos &&
                                properties(browser).find(L"Set View view ID:") ==
                                    std::wstring::npos &&
                                ListView_GetItemCount(links) == (conditional ? 1 : 0),
                            "Extended Set View body retained stale fields or candidate links");
                }
            }
        }
        for (const bool conditional : {false, true}) {
            for (unsigned target_case = 0; target_case < 5; ++target_case) {
                bytes = fixture();
                const unsigned prefix = conditional ? 12 : 0;
                const auto id = target_case == 3 ? 0u : target_case == 4 ? 0xffffffffu : 200u;
                stored_list_fixture::word(bytes, 86, target_case == 1 ? 0x4f : 0x4e);
                stored_list_fixture::word(bytes, 118, target_case == 1 ? 0x4f : 0x4e);
                stored_list_fixture::word(bytes, 129, target_case == 2 ? 0x4e : 0x35);
                stored_list_fixture::word(bytes, 161, target_case == 2 ? 0x4e : 0x35);
                stored_list_fixture::word(bytes, 332, 200);
                stored_list_fixture::word(bytes, 172, 0x53);
                stored_list_fixture::word(bytes, 204, 0x53);
                stored_list_fixture::word(bytes, 800, 201);
                stored_list_fixture::word(bytes, 650, prefix + 5);
                bytes[654] = conditional ? 0x85 : 5;
                stored_list_fixture::word(bytes, 800 + prefix, id);
                bytes[804 + prefix] = 255;
                write();
                SendMessageW(browser, WM_COMMAND, 3102, 0);
                const auto captures_before = captures;
                SetWindowTextW(GetDlgItem(browser, 3101), L"Raw interface control 255");
                require(ListView_GetItemCount(rows) == 1, "Interface control not searchable");
                select(rows, 0);
                fields = properties(browser);
                require(fields.find(L"Interface layout ID: " + std::to_wstring(id)) !=
                                std::wstring::npos &&
                            fields.find(L"Interface control byte (raw): 255") !=
                                std::wstring::npos &&
                            fields.find(L"Interface reference class: 0x0000004e (VCIFaceLayout)") !=
                                std::wstring::npos &&
                            fields.find(L"Action body unsupported") == std::wstring::npos &&
                            ListView_GetItemCount(links) == (conditional ? 2 : 1),
                        "Interface fields narrowed or static/predicate links lost");
                select(links, conditional ? 1 : 0);
                require(bool(IsWindowEnabled(GetDlgItem(browser, 3109))) == (target_case == 0),
                        "Interface wrong class, duplicate, null or missing target linked");
                if (target_case == 0) {
                    SendMessageW(browser, WM_COMMAND, 3109, 0);
                    require(text(GetDlgItem(browser, 3114)).find(L"VCIFaceLayout ID 200") !=
                                    std::wstring::npos &&
                                captures == captures_before,
                            "Interface Follow lost identity or loaded native state");
                    SendMessageW(browser, WM_COMMAND, 3110, 0);
                    require(properties(browser).find(L"Interface layout ID: 200") !=
                                    std::wstring::npos &&
                                captures == captures_before,
                            "Interface Back lost fields or recaptured native state");
                }
                if (conditional) {
                    select(links, 0);
                    require(IsWindowEnabled(GetDlgItem(browser, 3109)),
                            "Interface predicate link lost");
                    SendMessageW(browser, WM_COMMAND, 3109, 0);
                    require(text(GetDlgItem(browser, 3114)).find(L"VCVariable ID 201") !=
                                    std::wstring::npos &&
                                captures == captures_before,
                            "Interface predicate Follow lost identity or loaded native state");
                    SendMessageW(browser, WM_COMMAND, 3110, 0);
                    require(properties(browser).find(L"Interface control byte (raw): 255") !=
                                    std::wstring::npos &&
                                captures == captures_before,
                            "Interface predicate Back lost fields or recaptured native state");
                }
                const auto query = target_case == 4 ? L"0xffffffff"
                                                    : L"Interface layout ID " + std::to_wstring(id);
                SetWindowTextW(GetDlgItem(browser, 3101), query.c_str());
                require(ListView_GetItemCount(rows) == 1, "Interface full ID not searchable");
                require(text(GetDlgItem(browser, 3106)).find(L"Stored action payload") !=
                            std::wstring::npos,
                        "Interface complete raw payload lost");
                stored_list_fixture::word(bytes, 650, prefix + 6);
                write();
                SendMessageW(browser, WM_COMMAND, 3102, 0);
                SetWindowTextW(GetDlgItem(browser, 3101), L"300");
                select(rows, 0);
                require(
                    properties(browser).find(L"Action body unsupported") != std::wstring::npos &&
                        properties(browser).find(L"Interface layout ID:") == std::wstring::npos &&
                        ListView_GetItemCount(links) == (conditional ? 1 : 0),
                    "Extended Interface body retained stale fields or candidate links");
            }
        }
        for (const bool conditional : {false, true}) {
            for (unsigned variant = 0; variant < 3; ++variant) {
                bytes = fixture();
                bytes.resize(2200);
                const unsigned prefix = conditional ? 12 : 0;
                const auto body = 800 + prefix;
                const unsigned count = variant == 0 ? 0 : variant == 1 ? 2 : 255;
                stored_list_fixture::word(bytes, 172, 0x53);
                stored_list_fixture::word(bytes, 204, 0x53);
                stored_list_fixture::word(bytes, 800, 201);
                stored_list_fixture::word(bytes, 650, prefix + 16 + 5 * count);
                bytes[654] = conditional ? 0x86 : 6;
                std::fill_n(bytes.begin() + body, 15, variant == 2 ? 'A' : 0);
                if (variant == 1) {
                    bytes[body] = 'F';
                    bytes[body + 1] = 1;
                    bytes[body + 2] = 255;
                    bytes[body + 3] = '\\';
                    bytes[body + 14] = 128;
                }
                bytes[body + 15] = static_cast<std::uint8_t>(count);
                for (unsigned i = 0; i < count; ++i) {
                    stored_list_fixture::word(bytes, body + 16 + 4 * i, 0xffffffffu ^ i);
                    bytes[body + 16 + 4 * count + i] = static_cast<std::uint8_t>(255 - i);
                }
                write();
                SendMessageW(browser, WM_COMMAND, 3102, 0);
                const auto captures_before = captures;
                SetWindowTextW(GetDlgItem(browser, 3101), L"C++ Function name");
                require(ListView_GetItemCount(rows) == 1, "Function name not searchable");
                select(rows, 0);
                fields = properties(browser);
                const auto name = variant == 0   ? L""
                                  : variant == 1 ? L"F\\x01\\xff\\\\"
                                                 : L"AAAAAAAAAAAAAAA";
                require(fields.find(L"C++ Function name: " + std::wstring(name) + L"\n") !=
                                std::wstring::npos &&
                            fields.find(L"Function name NUL within 15 bytes: " +
                                        std::to_wstring(variant != 2)) != std::wstring::npos &&
                            fields.find(L"Function argument count: " + std::to_wstring(count)) !=
                                std::wstring::npos &&
                            ListView_GetItemCount(links) == (conditional ? 1 : 0),
                        "Function name escaped incorrectly, unbounded or linked as an object");
                if (count) {
                    require(fields.find(L"Function argument 0 word (raw): 4294967295") !=
                                    std::wstring::npos &&
                                fields.find(L"Function argument 0 kind (raw): 255") !=
                                    std::wstring::npos &&
                                fields.find(L"Function argument " + std::to_wstring(count - 1) +
                                            L" kind (raw): " + std::to_wstring(256 - count)) !=
                                    std::wstring::npos,
                            "Function argument arrays reordered or unknown kinds narrowed");
                    SetWindowTextW(GetDlgItem(browser, 3101), L"0xffffffff");
                    require(ListView_GetItemCount(rows) == 1, "Function full word not searchable");
                }
                if (variant == 1) {
                    require(fields.find(L"4601ff5c0000000000000000000080") != std::wstring::npos,
                            "Function name trailing bytes lost");
                    SetWindowTextW(GetDlgItem(browser, 3101), L"4601ff5c0000000000000000000080");
                    require(ListView_GetItemCount(rows) == 1, "Function raw name not searchable");
                }
                if (conditional) {
                    select(links, 0);
                    SendMessageW(browser, WM_COMMAND, 3109, 0);
                    require(text(GetDlgItem(browser, 3114)).find(L"VCVariable ID 201") !=
                                    std::wstring::npos &&
                                captures == captures_before,
                            "Function predicate Follow lost identity or recaptured native state");
                    SendMessageW(browser, WM_COMMAND, 3110, 0);
                    require(properties(browser).find(L"Function argument count:") !=
                                    std::wstring::npos &&
                                captures == captures_before,
                            "Function predicate Back lost fields or recaptured native state");
                }
                require(text(GetDlgItem(browser, 3106)).find(L"Stored action payload") !=
                            std::wstring::npos,
                        "Function complete raw payload lost");
                for (const auto length : {prefix + 15, prefix + 17 + 5 * count}) {
                    stored_list_fixture::word(bytes, 650, length);
                    write();
                    SendMessageW(browser, WM_COMMAND, 3102, 0);
                    SetWindowTextW(GetDlgItem(browser, 3101), L"300");
                    select(rows, 0);
                    require(properties(browser).find(L"Action body unsupported") !=
                                    std::wstring::npos &&
                                properties(browser).find(L"Function argument count:") ==
                                    std::wstring::npos &&
                                ListView_GetItemCount(links) == (conditional ? 1 : 0),
                            "Malformed Function extent retained stale fields or links");
                }
            }
        }
        for (const bool conditional : {false, true}) {
            for (unsigned target_case = 0; target_case < 5; ++target_case) {
                bytes = fixture();
                const unsigned prefix = conditional ? 12 : 0;
                const auto id = target_case == 3 ? 0u : target_case == 4 ? 0xffffffffu : 200u;
                stored_list_fixture::word(bytes, 86, target_case == 1 ? 0x35 : 0x36);
                stored_list_fixture::word(bytes, 118, target_case == 1 ? 0x35 : 0x36);
                stored_list_fixture::word(bytes, 129, target_case == 2 ? 0x36 : 0x35);
                stored_list_fixture::word(bytes, 161, target_case == 2 ? 0x36 : 0x35);
                stored_list_fixture::word(bytes, 332, 200);
                stored_list_fixture::word(bytes, 172, 0x53);
                stored_list_fixture::word(bytes, 204, 0x53);
                stored_list_fixture::word(bytes, 800, 201);
                stored_list_fixture::word(bytes, 650, prefix + 10);
                bytes[654] = conditional ? 0x87 : 7;
                stored_list_fixture::word(bytes, 800 + prefix, id);
                bytes[804 + prefix] = 255;
                bytes[805 + prefix] = 128;
                bytes[806 + prefix] = 128;
                bytes[807 + prefix] = 0;
                bytes[808 + prefix] = 255;
                bytes[809 + prefix] = 255;
                write();
                SendMessageW(browser, WM_COMMAND, 3102, 0);
                const auto captures_before = captures;
                SetWindowTextW(GetDlgItem(browser, 3101), L"Raw sound selector 255");
                require(ListView_GetItemCount(rows) == 1, "3D Sound control not searchable");
                select(rows, 0);
                fields = properties(browser);
                require(fields.find(L"3D Sound asset-list ID: " + std::to_wstring(id)) !=
                                std::wstring::npos &&
                            fields.find(L"3D Sound selector (raw): 255") != std::wstring::npos &&
                            fields.find(L"3D Sound reference class: 0x00000036 (VCAssetRefList)") !=
                                std::wstring::npos &&
                            fields.find(L"3D Sound byte at body +5 (raw): 128") !=
                                std::wstring::npos &&
                            fields.find(L"3D Sound word at body +6 (raw): 32768") !=
                                std::wstring::npos &&
                            fields.find(L"3D Sound word at body +6 (signed): -32768") !=
                                std::wstring::npos &&
                            fields.find(L"3D Sound word at body +8 (raw): 65535") !=
                                std::wstring::npos &&
                            fields.find(L"3D Sound word at body +8 (signed): -1") !=
                                std::wstring::npos &&
                            fields.find(L"Action body unsupported") == std::wstring::npos &&
                            ListView_GetItemCount(links) == (conditional ? 2 : 1),
                        "3D Sound fields narrowed or static/predicate links lost");
                select(links, conditional ? 1 : 0);
                require(bool(IsWindowEnabled(GetDlgItem(browser, 3109))) == (target_case == 0),
                        "3D Sound wrong class, duplicate, null or missing target linked");
                if (target_case == 0) {
                    SendMessageW(browser, WM_COMMAND, 3109, 0);
                    require(text(GetDlgItem(browser, 3114)).find(L"VCAssetRefList ID 200") !=
                                    std::wstring::npos &&
                                captures == captures_before,
                            "3D Sound Follow lost identity or loaded native state");
                    SendMessageW(browser, WM_COMMAND, 3110, 0);
                    require(properties(browser).find(L"3D Sound asset-list ID: 200") !=
                                    std::wstring::npos &&
                                captures == captures_before,
                            "3D Sound Back lost fields or recaptured native state");
                }
                if (conditional) {
                    select(links, 0);
                    require(IsWindowEnabled(GetDlgItem(browser, 3109)),
                            "3D Sound predicate link lost");
                    SendMessageW(browser, WM_COMMAND, 3109, 0);
                    require(text(GetDlgItem(browser, 3114)).find(L"VCVariable ID 201") !=
                                    std::wstring::npos &&
                                captures == captures_before,
                            "3D Sound predicate Follow lost identity or loaded native state");
                    SendMessageW(browser, WM_COMMAND, 3110, 0);
                    require(properties(browser).find(L"3D Sound selector (raw): 255") !=
                                    std::wstring::npos &&
                                captures == captures_before,
                            "3D Sound predicate Back lost fields or recaptured native state");
                }
                const auto query = target_case == 4
                                       ? L"0xffffffff"
                                       : L"3D Sound asset-list ID " + std::to_wstring(id);
                SetWindowTextW(GetDlgItem(browser, 3101), query.c_str());
                require(ListView_GetItemCount(rows) == 1, "3D Sound full ID not searchable");
                for (const auto sound_query :
                     {L"Raw sound byte +5 128", L"Sound word +6 (raw) 32768", L"(signed) -32768",
                      L"Sound word +8 (raw) 65535", L"0x00008000"}) {
                    SetWindowTextW(GetDlgItem(browser, 3101), sound_query);
                    require(ListView_GetItemCount(rows) == 1,
                            "3D Sound raw/signed fields not searchable");
                }
                require(text(GetDlgItem(browser, 3106)).find(L"Stored action payload") !=
                            std::wstring::npos,
                        "3D Sound complete raw payload lost");
                stored_list_fixture::word(bytes, 650, prefix + 11);
                write();
                SendMessageW(browser, WM_COMMAND, 3102, 0);
                SetWindowTextW(GetDlgItem(browser, 3101), L"300");
                select(rows, 0);
                require(properties(browser).find(L"Action body unsupported") !=
                                std::wstring::npos &&
                            properties(browser).find(L"3D Sound asset-list ID:") ==
                                std::wstring::npos &&
                            ListView_GetItemCount(links) == (conditional ? 1 : 0),
                        "Extended 3D Sound body retained stale fields or candidate links");
            }
        }
        SetWindowTextW(GetDlgItem(browser, 3101), L"300");
        for (unsigned failure = 0; failure < 3; ++failure) {
            bytes = fixture();
            if (failure == 0) {
                stored_list_fixture::word(bytes, 642, 2);
            } else if (failure == 1) {
                stored_list_fixture::word(bytes, 646, 256);
            } else {
                stored_list_fixture::word(bytes, 650, 4097);
            }
            write();
            SendMessageW(browser, WM_COMMAND, 3102, 0);
            select(rows, 0);
            require(properties(browser).find(L"decoding unavailable") != std::wstring::npos &&
                        ListView_GetItemCount(links) == 0 &&
                        text(GetDlgItem(browser, 3106)).find(L"Stored action payload") ==
                            std::wstring::npos,
                    "Malformed refresh retained fields, payload or links");
        }
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Stored action endian, search, static links and invalidation passed\n";
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
