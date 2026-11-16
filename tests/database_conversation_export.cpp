#include "database_conversation_export_fixture.h"
#include "devtools/database/browser_state.h"
#include <commdlg.h>
#include <dlgs.h>
#include <fstream>
#include <iostream>

namespace ui = devtools::database_browser;
using stored_action_browser_fixture::require;

namespace {
std::wstring dialog_output;
bool dialog_seen = false, dialog_cancel = false;
UINT_PTR dialog_timer = 0;

BOOL CALLBACK filename_control(HWND window, LPARAM result) {
    wchar_t type[32]{};
    GetClassNameW(window, type, 32);
    const auto id = GetDlgCtrlID(window);
    if (std::wstring(type) == L"Edit" && (id == edt1 || id == cmb13 || id == 1001)) {
        *reinterpret_cast<HWND*>(result) = window;
        return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK handle_dialog(HWND window, LPARAM) {
    wchar_t title[128]{};
    GetWindowTextW(window, title, 128);
    if (std::wstring(title) != L"Export conversation reference to a new file") {
        return TRUE;
    }
    HWND filename = nullptr;
    EnumChildWindows(window, filename_control, reinterpret_cast<LPARAM>(&filename));
    if (!dialog_cancel && !filename) {
        return TRUE;
    }
    KillTimer(nullptr, dialog_timer);
    dialog_seen = true;
    if (!dialog_cancel) {
        SetWindowTextW(filename, dialog_output.c_str());
    }
    PostMessageW(window, WM_COMMAND, dialog_cancel ? IDCANCEL : IDOK, 0);
    return FALSE;
}

void CALLBACK dialog_tick(HWND, UINT, UINT_PTR, DWORD) {
    EnumThreadWindows(GetCurrentThreadId(), handle_dialog, 0);
}

void dialog_command(HWND window, const std::filesystem::path& output, bool cancel) {
    dialog_output = std::filesystem::absolute(output).wstring();
    dialog_cancel = cancel;
    dialog_seen = false;
    dialog_timer = SetTimer(nullptr, 0, 50, dialog_tick);
    require(dialog_timer != 0, "Cannot start dialog driver");
    SendMessageW(window, WM_COMMAND, ui::conversations_export_id, 0);
    KillTimer(nullptr, dialog_timer);
    require(dialog_seen, "Export button did not open a save dialog");
    require(std::filesystem::exists(output) != cancel, "Save dialog export/cancel failed");
}

void write_fixture(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

ui::Browser& state(HWND window) {
    return *reinterpret_cast<ui::Browser*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

void select_record(HWND window, std::uint32_t cls, std::uint32_t id) {
    auto& browser = state(window);
    ui::navigate(browser, {{{cls, id, false}}, {}, true}, false);
    ui::select_pane(browser, ui::Pane::conversations);
    ui::layout(browser);
}

void save(ui::Browser& browser, const std::filesystem::path& path) {
    ui::save_conversation_reference(browser, path);
    const auto expected = ui::conversation_reference_json(browser);
    std::ifstream input(path, std::ios::binary);
    const std::string actual{std::istreambuf_iterator<char>(input), {}};
    require(actual == expected, "Saved JSON differs from selected rows");
}

HWND create(HWND parent, const std::filesystem::path& path) {
    return devtools::create_database_browser(parent, GetModuleHandleW(nullptr),
                                             static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                             path.parent_path(), {}, {}, path);
}
}

int main(int argc, char** argv) {
    HWND parent = nullptr;
    try {
        wchar_t temporary[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temporary);
        const bool owned_output = argc < 2;
        const auto output = owned_output ? std::filesystem::path(temporary) /
                                               (L"xfiles-reference-export-" +
                                                std::to_wstring(GetCurrentProcessId()) + L"-" +
                                                std::to_wstring(GetTickCount64()))
                                         : std::filesystem::path(argv[1]);
        if (owned_output) {
            require(std::filesystem::create_directory(output),
                    "Cannot create owned export directory");
        }
        std::filesystem::create_directories(output / L"XV");
        const auto path = output / L"XFILES.HDB";
        auto bytes = conversation_export_fixture::make();
        write_fixture(path, bytes);
        std::ofstream(output / L"XV/19794.XMV", std::ios::binary).put('x');
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1400, 850, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent != nullptr, "Cannot create export host");
        for (int arg = 2; arg < argc; ++arg) {
            const auto installed = std::filesystem::path(argv[arg]);
            const auto probe = create(parent, installed);
            select_record(probe, 0x2d, 21993);
            require(std::any_of(state(probe).conversation_rows.begin(),
                                state(probe).conversation_rows.end(),
                                [](const auto& row) {
                                    return row.media == std::filesystem::path(L"xv/19794.xmv") &&
                                           row.columns[0].starts_with(L"Stock slot [2]");
                                }),
                    "Original Cook flashlight candidate is missing");
            require(std::any_of(state(probe).conversation_rows.begin(),
                                state(probe).conversation_rows.end(),
                                [](const auto& row) { return row.topic_id == 58356; }),
                    "Original Cook APB topic is missing");
            save(state(probe), output / (L"cook-" + std::to_wstring(arg - 2) + L".json"));
            DestroyWindow(probe);
        }
        const auto window = create(parent, path);
        auto& browser = state(window);
        for (const auto width : {204, 300, 500}) {
            ui::layout_conversations(browser, 0, 0, width, 500, ui::Pane::conversations);
            RECT previous{};
            for (const auto button : {browser.conversations_open, browser.conversations_follow,
                                      browser.conversations_export}) {
                RECT bounds{};
                GetWindowRect(button, &bounds);
                MapWindowPoints(HWND_DESKTOP, window, reinterpret_cast<POINT*>(&bounds), 2);
                require(bounds.left >= 0 && bounds.right <= width && bounds.bottom <= 500,
                        "Conversation action escaped a narrow pane");
                RECT overlap{};
                require(!IntersectRect(&overlap, &previous, &bounds),
                        "Conversation actions overlap");
                previous = bounds;
            }
        }
        ui::layout(browser);
        require(!IsWindowEnabled(browser.conversations_export), "Export enabled without selection");
        SendMessageW(window, WM_COMMAND, ui::conversations_export_id, 0);
        bool rejected = false;
        try {
            ui::save_conversation_reference(browser, output / L"no-selection.json");
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && !std::filesystem::exists(output / L"no-selection.json"),
                "No-selection save wrote a reference");
        select_record(window, 0x31, 301);
        require(IsWindowEnabled(browser.conversations_export), "Selected topic cannot export");
        require(browser.conversation_rows.size() == 13, "Question/response coverage changed");
        const std::uint32_t expected[] = {600, 601, 600, 602, 0, 999};
        for (std::size_t index = 0; index < std::size(expected); ++index) {
            for (const auto base : {1u, 7u}) {
                const auto& row = browser.conversation_rows[base + index];
                require(row.candidate_id == expected[index] && row.position == index &&
                            row.topic_id == 301 && row.list_id == 501,
                        "Export metadata lost candidate identity or duplicate/null order");
            }
        }
        require(browser.conversation_rows[2].columns[3] == L"Missing file" &&
                    browser.conversation_rows[4].media.empty() &&
                    browser.conversation_rows[5].columns[3] == L"Null entry" &&
                    browser.conversation_rows[6].columns[3] == L"Missing / unsupported",
                "Export fixture hid missing, unsafe or null candidates");
        require(ListView_GetNextItem(browser.conversations_list, -1, LVNI_SELECTED) < 0,
                "Topic export test unexpectedly selected a candidate");
        dialog_command(window, output / L"dialog.json", false);
        dialog_command(window, output / L"cancel.json", true);
        select_record(window, 0x2d, 400);
        save(browser, output / L"character.json");
        select_record(window, 0x32, 201);
        save(browser, output / L"list.json");
        const auto before = ui::conversation_reference_json(browser);
        rejected = false;
        try {
            ui::save_conversation_reference(browser, output / L"list.json");
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Export overwrote an existing file");
        std::ifstream existing(output / L"list.json", std::ios::binary);
        require(std::string(std::istreambuf_iterator<char>(existing), {}) == before,
                "Existing export was modified");
        existing.close();
        ui::update_conversations(browser, 0, 0);
        require(!IsWindowEnabled(browser.conversations_export),
                "Export stayed enabled after deselection");
        DestroyWindow(window);

        bytes[1280] = 0;
        write_fixture(path, bytes);
        auto probe = create(parent, path);
        select_record(probe, 0x31, 301);
        save(state(probe), output / L"malformed.json");
        DestroyWindow(probe);

        bytes = conversation_export_fixture::make();
        const std::uint8_t raw_name[] = {'A', '"', '\\', 1, 0x80, 0xff, 0, 'Z'};
        stored_list_fixture::word(bytes, 970, sizeof(raw_name));
        std::copy(std::begin(raw_name), std::end(raw_name), bytes.begin() + 2080);
        write_fixture(path, bytes);
        probe = create(parent, path);
        select_record(probe, 0x31, 301);
        save(state(probe), output / L"raw-name.json");
        auto& unicode = state(probe);
        unicode.path = output / L"Fran\u00e7ais-\U0001f6f8.HDB";
        unicode.conversation_rows.front().detail = L"\"\\\t\r\n\u00e9\U0001f6f8";
        unicode.conversation_rows.front().detail += L'\0';
        unicode.conversation_rows.front().detail += L"end";
        save(unicode, output / L"unicode.json");
        unicode.conversation_rows.front().detail.assign(1, wchar_t(0xd800));
        rejected = false;
        try {
            ui::save_conversation_reference(unicode, output / L"invalid-unicode.json");
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && !std::filesystem::exists(output / L"invalid-unicode.json"),
                "Invalid Unicode created a malformed JSON file");
        DestroyWindow(probe);

        bytes = conversation_export_fixture::make();
        bytes.resize(4096 + 65536, 'A');
        stored_list_fixture::word(bytes, 966, 4096);
        stored_list_fixture::word(bytes, 970, 65536);
        write_fixture(path, bytes);
        probe = create(parent, path);
        select_record(probe, 0x31, 301);
        save(state(probe), output / L"large-name.json");
        DestroyWindow(probe);

        bytes = conversation_export_fixture::make();
        stored_list_fixture::word(bytes, 1094, 4096);
        stored_list_fixture::word(bytes, 1098, 4096);
        bytes.resize(25000);
        stored_list_fixture::node(bytes, 4096, false, {});
        bytes[4103] = 17;
        for (unsigned branch = 0; branch < 17; ++branch) {
            const auto count = std::min(255u, 4096 - branch * 255);
            const auto mark = 8192 + branch * 1028;
            stored_list_fixture::word(bytes, 4104 + branch * 4, mark);
            stored_list_fixture::node(bytes, mark, true, std::vector<std::uint32_t>(count, 999));
        }
        write_fixture(path, bytes);
        probe = create(parent, path);
        select_record(probe, 0x32, 201);
        require(!state(probe).conversations_limited &&
                    state(probe).conversation_rows.size() == 4096,
                "Exactly 4096 rows were falsely marked as display limited");
        save(state(probe), output / L"exact-limit.json");
        DestroyWindow(probe);
        for (unsigned branch = 0; branch < 17; ++branch) {
            const auto count = std::min(255u, 4096 - branch * 255);
            const auto mark = 8192 + branch * 1028;
            stored_list_fixture::node(bytes, mark, true, std::vector<std::uint32_t>(count, 301));
        }
        write_fixture(path, bytes);
        probe = create(parent, path);
        select_record(probe, 0x32, 201);
        require(state(probe).conversations_limited && state(probe).conversation_rows.size() == 4097,
                "Actual large association did not disclose its row limit");
        save(state(probe), output / L"limited.json");
        state(probe).stored.truncated = true;
        save(state(probe), output / L"index-incomplete.json");
        DestroyWindow(probe);

        DestroyWindow(parent);
        if (owned_output) {
            std::filesystem::remove_all(output);
        }
        std::cout << "Reference model, parser fixtures, selection, save dialog and new-file checks "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
