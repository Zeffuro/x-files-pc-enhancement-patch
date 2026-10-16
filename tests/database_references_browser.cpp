#include "stored_action_browser_fixture.h"
#include "devtools/database/browser_state.h"
#include <fstream>
#include <iostream>

namespace {
using namespace stored_action_browser_fixture;
using namespace devtools::database_browser;

void write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    require(bool(file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size())),
            "Cannot write reference fixture");
}

void variable(Browser& state) {
    navigate(state, {devtools::DatabaseObjectKey{0x53, 200, false}, {}, true}, false);
    require(selected_row(state) && selected_row(state)->stored,
            "Cannot select exact stored variable");
}
}

int main() {
    using namespace stored_action_browser_fixture;
    using namespace devtools::database_browser;
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-references-" + std::to_wstring(GetCurrentProcessId()));
    HWND parent = nullptr;
    try {
        std::filesystem::create_directories(root);
        const auto hdb = root / L"XFILES.HDB", gam = root / L"XFILES.GAM";
        auto bytes = fixture();
        write(hdb, bytes);
        write(gam, bytes);
        const auto module = GetModuleHandleW(nullptr);
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 800, nullptr,
                                 nullptr, module, nullptr);
        require(parent != nullptr, "Cannot create reference host");
        const auto window = devtools::create_database_browser(
            parent, module, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), root, {}, {},
            gam);
        auto& state = *reinterpret_cast<Browser*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        variable(state);
        auto from = std::find_if(state.links.begin(), state.links.end(), [&](const auto& link) {
            return link.columns[0] == L"From" && link.destination.database_path == hdb;
        });
        require(from != state.links.end() &&
                    properties(window).find(L"References (stored definitions)") !=
                        std::wstring::npos,
                "Standalone GAM did not discover its sibling HDB callers");
        const auto choice = static_cast<int>(from - state.links.begin());
        select(state.relations, choice);
        SendMessageW(window, WM_COMMAND, 3109, 0);
        require(state.path == hdb && selected_row(state) && selected_row(state)->stored &&
                    state.stored.records[*selected_row(state)->stored].class_id == 0x41 &&
                    state.stored.records[*selected_row(state)->stored].id == 300,
                "Reference Follow lost its source file or exact action identity");
        SendMessageW(window, WM_COMMAND, 3110, 0);
        require(state.path == gam && selected_row(state) && selected_row(state)->stored &&
                    state.stored.records[*selected_row(state)->stored].class_id == 0x53,
                "Reference Back did not restore the GAM selection");
        const auto before = state.references->references.size();
        stored_list_fixture::word(bytes, 800, 201);
        stored_list_fixture::word(bytes, 812, 201);
        write(hdb, bytes);
        SendMessageW(window, WM_COMMAND, 3102, 0);
        variable(state);
        require(state.references && state.references->references.size() == before &&
                    std::none_of(state.links.begin(), state.links.end(),
                                 [&](const auto& link) {
                                     return link.columns[0] == L"From" &&
                                            link.destination.database_path == hdb;
                                 }),
                "Refresh retained stale companion-file references");
        navigate(state, {devtools::DatabaseObjectKey{0x53, 200, false}, {}, true, hdb}, false);
        require(properties(window).find(L"Incoming references: 0") != std::wstring::npos,
                "Same-ID HDB variable captured GAM references");
        std::filesystem::remove(gam);
        SendMessageW(window, WM_COMMAND, 3102, 0);
        variable(state);
        require(properties(window).find(L"Companion HDB/GAM unavailable") != std::wstring::npos,
                "Missing companion file was not disclosed");
        DestroyWindow(parent);
        parent = nullptr;
        std::filesystem::remove_all(root);
        std::cout << "Reference source jumps, namespace isolation and Refresh invalidation pass.\n";
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
