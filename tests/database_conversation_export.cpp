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
std::wstring dialog_diagnostics;
std::wstring dialog_initial, dialog_history;
bool dialog_seen = false, dialog_cancel = false;
bool dialog_value_set = false, dialog_save_posted = false, dialog_timed_out = false;
bool dialog_reset_requested = false, dialog_reset_done = false;
unsigned dialog_writes = 0;
ULONGLONG dialog_started = 0;
UINT_PTR dialog_timer = 0;

std::wstring control_type(HWND window) {
    wchar_t type[128]{};
    GetClassNameW(window, type, 128);
    return type;
}

bool enter_filename(HWND edit, const std::wstring& value) {
    SendMessageW(edit, EM_SETSEL, 0, -1);
    SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(value.c_str()));
    return ui::text(edit) == value;
}

BOOL CALLBACK filename_control(HWND window, LPARAM result) {
    wchar_t type[32]{};
    GetClassNameW(window, type, 32);
    const auto id = GetDlgCtrlID(window);
    const auto parent = GetParent(window);
    const auto parent_type = control_type(parent);
    const auto parent_id = GetDlgCtrlID(parent);
    if (std::wstring(type) == L"Edit") {
        dialog_diagnostics += L"\nEdit id=" + std::to_wstring(id) + L" parent_id=" +
                              std::to_wstring(GetDlgCtrlID(GetParent(window))) + L" parent_type=" +
                              control_type(GetParent(window)) + L" text=" + ui::text(window);
    }
    const bool filename_parent =
        parent_type == L"ComboBox" &&
        (parent_id == cmb13 || (parent_id == 0 && (id == 1001 || id == cmb13)));
    if (std::wstring(type) == L"Edit" &&
        (filename_parent || (id == edt1 && parent_type == L"#32770"))) {
        auto& selected = *reinterpret_cast<HWND*>(result);
        if (!selected) {
            selected = window;
        }
    }
    return TRUE;
}

BOOL CALLBACK handle_dialog(HWND window, LPARAM) {
    wchar_t title[128]{};
    GetWindowTextW(window, title, 128);
    if (std::wstring(title) != L"Export conversation reference to a new file") {
        return TRUE;
    }
    dialog_diagnostics = L"default_id=" + std::to_wstring(SendMessageW(window, DM_GETDEFID, 0, 0)) +
                         L" save_enabled=" +
                         std::to_wstring(IsWindowEnabled(GetDlgItem(window, IDOK)));
    HWND filename = nullptr;
    EnumChildWindows(window, filename_control, reinterpret_cast<LPARAM>(&filename));
    dialog_seen = true;
    if (GetTickCount64() - dialog_started > 30000) {
        dialog_timed_out = true;
        KillTimer(nullptr, dialog_timer);
        std::wcerr << L"Dialog driver deadline:" << dialog_diagnostics << L'\n' << dialog_history;
        PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
        return FALSE;
    }
    if (dialog_cancel) {
        KillTimer(nullptr, dialog_timer);
        PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
        return FALSE;
    }
    if (!filename || dialog_save_posted || !IsWindowEnabled(GetDlgItem(window, IDOK))) {
        return FALSE;
    }
    const auto current = ui::text(filename);
    if (!dialog_value_set) {
        if (current.empty()) {
            return FALSE;
        }
        if (dialog_initial.empty()) {
            dialog_initial = current;
        }
        const auto changed = enter_filename(filename, dialog_output);
        ++dialog_writes;
        dialog_history += L"\nselected_edit_id=" + std::to_wstring(GetDlgCtrlID(filename)) +
                          L" set_result=" + std::to_wstring(changed) + L" readback=" +
                          ui::text(filename);
        dialog_value_set = changed != FALSE;
        return FALSE;
    }
    if (dialog_reset_requested && !dialog_reset_done) {
        SetWindowTextW(filename, dialog_initial.c_str());
        dialog_reset_done = true;
        return FALSE;
    }
    if (current != dialog_output) {
        dialog_value_set = false;
        dialog_history += L"\nFilename changed before Save: " + current;
        return FALSE;
    }
    dialog_save_posted = true;
    PostMessageW(window, WM_COMMAND, IDOK, 0);
    return FALSE;
}

void CALLBACK dialog_tick(HWND, UINT, UINT_PTR, DWORD) {
    EnumThreadWindows(GetCurrentThreadId(), handle_dialog, 0);
}

void dialog_command(HWND window, const std::filesystem::path& output, bool cancel,
                    bool reset = false) {
    dialog_output = std::filesystem::absolute(output).wstring();
    dialog_cancel = cancel;
    dialog_seen = false;
    dialog_value_set = dialog_save_posted = dialog_timed_out = dialog_reset_done = false;
    dialog_reset_requested = reset;
    dialog_writes = 0;
    dialog_started = GetTickCount64();
    dialog_initial.clear();
    dialog_history.clear();
    dialog_diagnostics.clear();
    dialog_timer = SetTimer(nullptr, 0, 50, dialog_tick);
    require(dialog_timer != 0, "Cannot start dialog driver");
    SendMessageW(window, WM_COMMAND, ui::conversations_export_id, 0);
    KillTimer(nullptr, dialog_timer);
    if (!dialog_seen || dialog_timed_out || std::filesystem::exists(output) == cancel) {
        std::wcerr << L"Dialog phase=" << (cancel ? L"cancel" : L"save") << L" expected="
                   << output.wstring() << L" absolute=" << dialog_output << L" seen=" << dialog_seen
                   << L" expected_exists=" << std::filesystem::exists(output)
                   << L"\nBrowser status=" << ui::text(GetDlgItem(window, ui::status_id))
                   << L"\nInitial filename=" << dialog_initial << L"\nDialog controls="
                   << dialog_diagnostics << L"\nActions=" << dialog_history << L'\n';
    }
    require(dialog_seen, "Export button did not open a save dialog");
    require(!dialog_timed_out, "Export dialog driver did not complete before its deadline");
    require(std::filesystem::exists(output) != cancel, "Save dialog export/cancel failed");
    require(!reset || (dialog_reset_done && dialog_writes >= 2),
            "Filename initialization overwrite was not exercised");
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

struct FilenameModel {
    HWND combo;
    std::wstring value;
    unsigned edits = 0;
};

LRESULT CALLBACK filename_model(HWND window, UINT message, WPARAM parameter, LPARAM data, UINT_PTR,
                                DWORD_PTR context) {
    auto& model = *reinterpret_cast<FilenameModel*>(context);
    if (message == WM_COMMAND && HIWORD(parameter) == CBN_EDITCHANGE &&
        reinterpret_cast<HWND>(data) == model.combo) {
        model.value = ui::text(model.combo);
        ++model.edits;
    }
    return DefSubclassProc(window, message, parameter, data);
}

void verify_filename_selector(HWND parent) {
    const auto fixture = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 400, 200, parent,
                                         nullptr, GetModuleHandleW(nullptr), nullptr);
    const auto filename =
        CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN, 0, 0, 200, 100, fixture,
                        nullptr, GetModuleHandleW(nullptr), nullptr);
    const auto decoy =
        CreateWindowExW(0, L"EDIT", L"Search", WS_CHILD, 0, 0, 200, 24, fixture,
                        reinterpret_cast<HMENU>(1001), GetModuleHandleW(nullptr), nullptr);
    const auto address =
        CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN, 0, 0, 200, 100, fixture,
                        reinterpret_cast<HMENU>(41477), GetModuleHandleW(nullptr), nullptr);
    require(fixture && filename && decoy && address,
            "Cannot create filename-selector regression fixture");
    SetWindowPos(decoy, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(address, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    COMBOBOXINFO info{sizeof(info)};
    require(GetComboBoxInfo(filename, &info), "Cannot inspect actual ComboBox filename Edit");
    HWND selected = nullptr;
    dialog_diagnostics.clear();
    EnumChildWindows(fixture, filename_control, reinterpret_cast<LPARAM>(&selected));
    if (selected != info.hwndItem) {
        std::wcerr << L"Filename selector decoy regression:" << dialog_diagnostics << L'\n';
    }
    require(selected == info.hwndItem, "Filename selector chose non-filename Edit1001");
    FilenameModel model{filename, L"initial.json"};
    SetWindowTextW(info.hwndItem, model.value.c_str());
    require(SetWindowSubclass(fixture, filename_model, 1, reinterpret_cast<DWORD_PTR>(&model)),
            "Cannot observe actual filename ComboBox edit notifications");
    const std::wstring desired = L"C:\\owned-export\\dialog.json";
    require(enter_filename(info.hwndItem, desired), "Cannot enter filename through its Edit");
    if (model.value != desired || !model.edits) {
        std::wcerr << L"Filename ComboBox model=" << model.value << L" visual="
                   << ui::text(info.hwndItem) << L" edit_notifications=" << model.edits << L'\n';
    }
    require(model.value == desired && model.edits,
            "Filename changed visually without updating the ComboBox model");
    DestroyWindow(fixture);
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
        verify_filename_selector(window);
        dialog_command(window, output / L"reset-dialog.json", false, true);
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
