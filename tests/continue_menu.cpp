#include "enhancements/ui/continue_menu.h"
#include "enhancements/game_ui.h"
#include "saves/recent.h"
#include "settings.h"
#include <iostream>
#include <stdexcept>

namespace {
Settings options;
std::vector<std::byte> native_image(0x300000);
const enhancements::game::Edition* profile = &enhancements::game::dvd;
bool attached = true, in_menu = true, modal = false, browser = false, pending = false;
bool transcript_open = false, selected = true, failed = false;
unsigned loads = 0, queries = 0, suspensions = 0, errors = 0;
std::filesystem::path loaded, newest = L"synthetic-newest.x";

void require(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

int& session() {
    return *reinterpret_cast<int*>(native_image.data() + profile->session_active);
}

int WINAPI report_error(HWND, LPCSTR, LPCSTR, UINT) {
    ++errors;
    return IDOK;
}
}

const Settings& settings() {
    return options;
}

namespace enhancements::game {
std::byte* executable_image() {
    return attached ? native_image.data() : nullptr;
}

const Edition& edition() {
    return *profile;
}

std::uintptr_t input_vtable() {
    return in_menu ? profile->main_menu : 0;
}

bool menu_confirmation_active() {
    return modal;
}
}

namespace enhancements {
bool checkpoint_available() {
    return !pending;
}

std::filesystem::path save_game_root() {
    return L"synthetic-game";
}

void load_checkpoint(HWND, const std::filesystem::path& path) {
    ++loads;
    loaded = path;
    pending = true;
}

void suspend_controller() {
    ++suspensions;
}
}

void trace_value(const char*, std::uint32_t) {}

namespace saves {
bool browser_active() {
    return browser;
}

std::optional<Slot> newest_save(const std::filesystem::path& path, bool current) {
    require(path == L"synthetic-game" &&
                current == (profile->application != enhancements::game::cd.application),
            "Return queried the wrong edition or folder");
    ++queries;
    if (failed) {
        throw std::runtime_error("Unavailable save folder");
    }
    if (!selected) {
        return std::nullopt;
    }
    Slot result;
    result.file = newest;
    return result;
}
}

namespace transcript {
bool active() {
    return transcript_open;
}
}

#define MessageBoxA report_error
#include "../src/enhancements/ui/continue_menu.cpp"
#undef MessageBoxA

namespace {
void align(HWND window, POINT target) {
    POINT cursor{};
    require(GetCursorPos(&cursor) &&
                SetWindowPos(window, nullptr, cursor.x - target.x, cursor.y - target.y, 640, 480,
                             SWP_NOZORDER | SWP_NOACTIVATE),
            "Cannot position the hidden input owner");
}

void exercise(HWND window, const enhancements::game::Edition& edition) {
    using namespace enhancements;
    profile = &edition;
    session() = 0;
    options.continue_latest = true;
    pending = false;
    loads = queries = suspensions = errors = 0;
    const auto click = [&](UINT message) { return continue_message(window, message, 0, 0); };
    align(window, {555, 243});
    require(!click(WM_MOUSEMOVE) && !click(WM_TIMER) && !queries,
            "Idle Return scanned saved games");
    options.continue_latest = false;
    require(!click(WM_LBUTTONDOWN) && !click(WM_LBUTTONUP) && !queries,
            "Disabled option intercepted native Return");
    options.continue_latest = true;
    session() = 1;
    require(!click(WM_LBUTTONDOWN) && !click(WM_LBUTTONUP) && !queries,
            "Paused session was replaced by a saved game");
    session() = 0;
    for (auto* blocked : {&modal, &browser, &pending, &transcript_open}) {
        *blocked = true;
        require(!click(WM_LBUTTONDOWN) && !click(WM_LBUTTONUP) && !queries,
                "Unavailable menu intercepted Return");
        *blocked = false;
    }
    for (auto* enabled : {&attached, &in_menu}) {
        *enabled = false;
        require(!click(WM_LBUTTONDOWN) && !click(WM_LBUTTONUP) && !queries,
                "Non-menu context intercepted Return");
        *enabled = true;
    }
    align(window, {380, 451});
    require(!click(WM_LBUTTONDOWN) && !click(WM_LBUTTONUP) && !queries,
            "Removed Continue button still intercepted clicks");
    align(window, {555, 265});
    require(!click(WM_LBUTTONDOWN) && !queries, "Return extended beyond native hit bounds");
    align(window, {555, 243});
    selected = false;
    require(!click(WM_LBUTTONDOWN) && !click(WM_LBUTTONUP) && queries == 1 && !loads,
            "No saved game prevented native Return fallback");
    selected = true;
    failed = true;
    require(!click(WM_LBUTTONDOWN) && !click(WM_LBUTTONUP) && !errors,
            "Failed discovery prevented native Return fallback");
    failed = false;
    require(click(WM_LBUTTONDOWN), "Startup Return press was not consumed");
    const auto before = queries;
    newest = L"newer-between-press-and-release.x";
    require(click(WM_LBUTTONUP) && loads == 1 && suspensions == 1 && loaded == newest &&
                queries == before + 1,
            "Return did not reselect and load the newest compatible save");
    require(!click(WM_LBUTTONDBLCLK) && !click(WM_LBUTTONUP) && loads == 1,
            "Pending load allowed duplicate Return activation");
    pending = false;
    require(click(WM_LBUTTONDOWN), "Stale save setup failed");
    selected = false;
    require(click(WM_LBUTTONUP) && loads == 1, "Return loaded a disappeared save");
    selected = true;
    require(click(WM_LBUTTONDOWN), "Release failure setup failed");
    failed = true;
    require(click(WM_LBUTTONUP) && loads == 1 && errors == 1,
            "Release failure leaked a native click or loaded");
    failed = false;
    require(click(WM_LBUTTONDOWN), "Modal transition setup failed");
    modal = true;
    require(click(WM_LBUTTONUP) && loads == 1, "Modal transition leaked a click or loaded");
    modal = false;
    require(click(WM_LBUTTONDOWN), "Session transition setup failed");
    session() = 1;
    require(click(WM_LBUTTONUP) && loads == 1, "New session was replaced during a Return press");
    session() = 0;
    require(click(WM_LBUTTONDOWN), "Option transition setup failed");
    options.continue_latest = false;
    require(click(WM_LBUTTONUP) && loads == 1, "Disabled option loaded after a pending press");
    options.continue_latest = true;
    require(click(WM_LBUTTONDOWN), "Drag cancellation setup failed");
    align(window, {400, 243});
    require(click(WM_LBUTTONUP) && loads == 1, "Dragging outside Return still loaded");
    align(window, {555, 243});
    for (const auto message : {WM_KILLFOCUS, WM_ACTIVATEAPP}) {
        require(click(WM_LBUTTONDOWN), "Focus cancellation setup failed");
        require(!click(message) && !click(WM_LBUTTONUP) && loads == 1,
                "Focus loss retained a pending Return press");
    }
    require(click(WM_LBUTTONDBLCLK) && click(WM_LBUTTONUP) && loads == 2,
            "Fresh Return double-click did not load exactly once");
    pending = false;
    release_continue_menu();
    require(!click(WM_LBUTTONUP), "Release retained a pending Return press");
    require(!FindWindowW(L"XFilesContinueMenu", nullptr), "Continue created an extra popup");
}
}

int main() {
    HWND window = nullptr;
    try {
        window = CreateWindowExW(0, L"STATIC", L"Native Return test", WS_POPUP, 0, 0, 640, 480,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Cannot create hidden input owner");
        for (const auto* build_profile :
             {&native_game::profile_cd_10012, &native_game::profile_cd_10019,
              &native_game::profile_cd_10020, &native_game::profile_dvd_20000}) {
            exercise(window, *build_profile);
        }
        DestroyWindow(window);
        std::cout << "All four Return profiles, startup selection and native fallbacks passed\n";
        return 0;
    } catch (const std::exception& error) {
        enhancements::release_continue_menu();
        if (window) {
            DestroyWindow(window);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
