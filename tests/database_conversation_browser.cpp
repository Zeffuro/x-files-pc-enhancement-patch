#include "stored_action_browser_fixture.h"
#include "devtools/database/browser_state.h"
#include <fstream>
#include <iostream>

using namespace stored_action_browser_fixture;
namespace browser_ui = devtools::database_browser;

namespace {
std::vector<std::uint8_t> browser_fixture() {
    using namespace stored_list_fixture;
    std::vector<std::uint8_t> bytes(4096);
    word(bytes, 0, 5);
    word(bytes, 8, 32);
    word(bytes, 20, 0x501);
    word(bytes, 24, 0x40000);
    word(bytes, 28, 256);
    node(bytes, 32, true, {});
    bytes[39] = 6;
    const std::uint32_t classes[] = {0x31, 0x2f, 0x2d, 0x32, 0x36, 0x35};
    const std::vector<std::vector<std::uint32_t>> ids{{300, 301}, {200, 201}, {400},
                                                      {201},      {500, 501}, {600, 601, 602}};
    const std::vector<std::vector<std::uint32_t>> offsets{
        {768, 832}, {896, 960}, {1024}, {1088}, {1152, 1216}, {1280, 1344, 1408}};
    for (unsigned i = 0; i < 6; ++i) {
        const auto entry = 40 + i * 43, index = 320 + i * 40;
        word(bytes, entry + 3, classes[i]);
        bytes[entry + 10] = 1;
        word(bytes, entry + 27, 7);
        word(bytes, entry + 31, index);
        word(bytes, entry + 35, classes[i]);
        word(bytes, entry + 39, 0x6e756c6c);
        node(bytes, index, true, {});
        bytes[index + 7] = static_cast<std::uint8_t>(ids[i].size());
        for (std::size_t j = 0; j < ids[i].size(); ++j) {
            word(bytes, index + 8 + 8 * j, offsets[i][j]);
            word(bytes, index + 12 + 8 * j, ids[i][j]);
            bytes[offsets[i][j]] = 0xc3;
            word(bytes, offsets[i][j] + 2, 1);
        }
    }
    word(bytes, 774, 200);
    word(bytes, 794, 500);
    word(bytes, 838, 201);
    word(bytes, 854, 501);
    word(bytes, 846, 501);
    const auto blob = [&](std::size_t at, std::size_t mark, const std::string& value) {
        word(bytes, at + 6, static_cast<std::uint32_t>(mark));
        word(bytes, at + 10, static_cast<std::uint32_t>(value.size()));
        std::copy(value.begin(), value.end(), bytes.begin() + mark);
    };
    blob(896, 2048, "Atoms");
    blob(960, 2080, "Will you file these APBs?");
    word(bytes, 1046, 201);
    const auto listing = [&](std::size_t at, std::size_t mark,
                             const std::vector<std::uint32_t>& members) {
        word(bytes, at + 6, static_cast<std::uint32_t>(members.size()));
        word(bytes, at + 10, static_cast<std::uint32_t>(mark));
        word(bytes, at + 14, 11);
        word(bytes, at + 18, 0x6e756c6c);
        word(bytes, at + 22, 1);
        node(bytes, mark, true, members);
    };
    listing(1088, 2176, {300, 301, 0, 301});
    listing(1152, 2240, {601, 0, 600, 601, 601, 601, 602, 600, 601});
    listing(1216, 2304, {600, 601, 600, 602, 0, 999});
    blob(1280, 2400,
         "\2"
         "12XV\x7f"
         "19794.xmv\1"
         "0");
    blob(1344, 2464,
         "\2"
         "12XV\x7f"
         "19790.xmv\1"
         "0");
    blob(1408, 2528,
         "\2"
         "15../XV\x7f"
         "00000.xmv\1"
         "0");
    return bytes;
}

int find(HWND list, int column, const std::wstring& query) {
    for (int at = 0; at < ListView_GetItemCount(list); ++at) {
        wchar_t value[4096]{};
        ListView_GetItemText(list, at, column, value, 4096);
        if (std::wstring(value).find(query) != std::wstring::npos) {
            return at;
        }
    }
    return -1;
}

void select_record(HWND window, std::uint32_t cls, std::uint32_t id) {
    auto& state = *reinterpret_cast<browser_ui::Browser*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    browser_ui::navigate(state, {{{cls, id, false}}, {}, true}, false);
    browser_ui::select_pane(state, browser_ui::Pane::conversations);
    browser_ui::layout(state);
}

void check(HWND parent, const std::filesystem::path& root, const std::filesystem::path& path,
           bool installed) {
    std::vector<std::filesystem::path> opened;
    const auto window = devtools::create_database_browser(
        parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        root, [&](const auto& media) { opened.push_back(media); }, {}, path);
    const auto rows = GetDlgItem(window, browser_ui::conversations_list_id);
    const auto open = GetDlgItem(window, browser_ui::conversations_open_id);
    SendMessageW(window, WM_COMMAND, browser_ui::conversations_browse_id, 0);
    const auto topics = GetDlgItem(window, browser_ui::list_id);
    require(ListView_GetItemCount(topics) == (installed ? 441 : 2),
            "Conversation browse lost coverage");
    SetWindowTextW(GetDlgItem(window, browser_ui::search_id), installed ? L"APB's" : L"APBs?");
    require(ListView_GetItemCount(topics) == 1, "Topic names are not searchable");
    select(topics, 0);
    select_record(window, 0x31, installed ? 58356 : 301);
    const auto question = find(rows, 0, L"Question [0]");
    require(question >= 0, "Question candidate missing");
    select(rows, question);
    require(IsWindowEnabled(open), "Installed question cannot open in Library");
    SendMessageW(window, WM_COMMAND, browser_ui::conversations_open_id, 0);
    require(opened.size() == 1 &&
                opened.back() ==
                    std::filesystem::path(installed ? L"xv/19772.xmv" : L"xv/19794.xmv"),
            "Library callback chose the wrong asset");
    SendMessageW(window, WM_COMMAND, browser_ui::conversations_follow_id, 0);
    require(properties(window).find(installed ? L"ID: 19772" : L"ID: 600") != std::wstring::npos,
            "Follow record lost asset identity");
    SendMessageW(window, WM_COMMAND, browser_ui::back_id, 0);
    auto& state = *reinterpret_cast<browser_ui::Browser*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    require(browser_ui::selected_pane(state) == browser_ui::Pane::conversations,
            "Back did not restore conversation pane");
    select_record(window, 0x2d, installed ? 21993 : 400);
    if (installed) {
        SendMessageW(window, WM_COMMAND, browser_ui::reactions_browse_id, 0);
        SetWindowTextW(GetDlgItem(window, browser_ui::search_id), L"Cook");
        require(ListView_GetItemCount(topics) >= 2, "Character names are not searchable");
        select_record(window, 0x2d, 21993);
    }
    const auto flashlight = find(rows, 0, L"Stock slot [2]");
    require(flashlight >= 0, "Character stock slots missing");
    select(rows, flashlight);
    require(IsWindowEnabled(open), "Flashlight media unavailable");
    SendMessageW(window, WM_COMMAND, browser_ui::conversations_open_id, 0);
    require(opened.back() == std::filesystem::path(L"xv/19794.xmv"),
            "Stock slot routed to wrong media");
    const auto badge = find(rows, 0, L"Stock slot [4]");
    require(badge >= 0, "Badge slot missing");
    select(rows, badge);
    require(bool(IsWindowEnabled(open)) == installed, "Missing file availability is wrong");
    const auto before = opened.size();
    SendMessageW(window, WM_COMMAND, browser_ui::conversations_open_id, 0);
    require(opened.size() == before + (installed ? 1 : 0), "Missing file invoked Library callback");
    if (!installed) {
        require(find(rows, 0, L"Stock slot [7]") < 0 &&
                    find(rows, 0, L"Extra stored reply [7]") >= 0,
                "Extra list entries invented native stock slots");
        require(find(rows, 3, L"Null entry") >= 0 && find(rows, 3, L"No movie path") >= 0 &&
                    find(rows, 3, L"Missing / unsupported") >= 0,
                "Unresolved entries were hidden");
        const auto no_movie = find(rows, 3, L"No movie path");
        select(rows, no_movie);
        require(!IsWindowEnabled(open), "Unsafe path became a movie link");
        require(find(rows, 0, L"Question [2]") >= 0, "Duplicate candidate order lost");
    } else {
        const auto response = find(rows, 2, L"19744.xmv");
        require(response >= 0 && find(rows, 2, L"19774.xmv") >= 0,
                "Both installed APB response candidates must be visible");
    }
    DestroyWindow(window);
}
}

int main(int argc, char** argv) {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-conversation-browser-" + std::to_wstring(GetCurrentProcessId()));
    HWND parent = nullptr;
    try {
        std::filesystem::create_directories(root / L"XV");
        const auto path = root / L"XFILES.HDB";
        const auto bytes = browser_fixture();
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        std::ofstream(root / L"XV/19794.XMV", std::ios::binary).put('x');
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1400, 850, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent != nullptr, "Cannot create host");
        check(parent, root, path, false);
        auto large = bytes;
        large.resize(4096 + 65536, 'A');
        stored_list_fixture::word(large, 966, 4096);
        stored_list_fixture::word(large, 970, 65536);
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(large.data()),
                   static_cast<std::streamsize>(large.size()));
        const auto stress = devtools::create_database_browser(
            parent, GetModuleHandleW(nullptr), static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
            root, {}, {}, path);
        select_record(stress, 0x2d, 400);
        auto& stress_state =
            *reinterpret_cast<browser_ui::Browser*>(GetWindowLongPtrW(stress, GWLP_USERDATA));
        for (const auto& row : stress_state.conversation_rows) {
            require(row.columns[1].size() < 300 && row.detail.size() < 2048,
                    "Large topic name expanded every candidate without a display bound");
        }
        require(find(stress_state.conversations_list, 1, L"name preview") >= 0,
                "Large name truncation was not disclosed");
        DestroyWindow(stress);
        for (int arg = 1; arg < argc; ++arg) {
            const auto database = std::filesystem::path(argv[arg]);
            check(parent, database.parent_path(), database, true);
        }
        DestroyWindow(parent);
        std::filesystem::remove_all(root);
        std::cout << "Conversation explorer, safe Library links and Follow/Back checks passed\n";
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
