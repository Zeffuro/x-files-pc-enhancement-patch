#include "saves/browser.h"
#include "saves/browser_state.h"
#include "enhancements/game_ui.h"
#include "enhancements/quick_save.h"
#include "enhancements/scene_overlay.h"
#include "game/render/native_render.h"
#include "playback/inspection.h"
#include "settings.h"

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::byte* image = nullptr;
enhancements::game::Edition profile = enhancements::game::dvd;
alignas(enhancements::game::Application) std::array<std::byte, 0x300> application_bytes{};
auto* application = reinterpret_cast<enhancements::game::Application*>(application_bytes.data());
enhancements::game::MainView view{};
std::byte native_state{};
unsigned pauses = 0, resumes = 0, loads = 0, suspensions = 0;
bool pending = false, fail_load = false, menu = false;
Settings options;
native_game::CanvasSource source = nullptr;
saves::Browser* painted = nullptr;

void __stdcall native_pause(void* state, int paused, void* queue) {
    require(state == &native_state && queue == application_bytes.data() + 0x268,
            "Scene pause received the wrong native owner");
    paused ? ++pauses : ++resumes;
}

class Fixture {
    std::array<BYTE, 256> keyboard_state{};

public:
    HWND window = nullptr;

    Fixture() {
        image = static_cast<std::byte*>(
            VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        require(image != nullptr, "Cannot allocate the synthetic executable image");
        profile.application = 0x100;
        profile.pending_load = 0x108;
        profile.pause = 0x200;
        application->state = &native_state;
        application->view = &view;
        std::memcpy(image + profile.application, &application, sizeof(application));
        image[profile.pause] = std::byte{0x68};
        const auto callback = native_pause;
        std::memcpy(image + profile.pause + 1, &callback, sizeof(callback));
        image[profile.pause + 5] = std::byte{0xc3};
        FlushInstructionCache(GetCurrentProcess(), image, 4096);
        options.save_browser = true;
        require(GetKeyboardState(keyboard_state.data()) != FALSE,
                "Cannot preserve the test thread keyboard state");
        std::array<BYTE, 256> neutral_keys{};
        require(SetKeyboardState(neutral_keys.data()) != FALSE,
                "Cannot clear inherited test thread modifiers");
        POINT cursor{};
        require(GetCursorPos(&cursor) != FALSE, "Cannot obtain the existing cursor position");
        window = CreateWindowExW(0, L"STATIC", L"Browser lifecycle test", WS_POPUP, cursor.x - 241,
                                 cursor.y - 415, 640, 480, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Cannot create the hidden browser input window");
    }

    ~Fixture() {
        saves::release_browser();
        DestroyWindow(window);
        SetKeyboardState(keyboard_state.data());
        VirtualFree(image, 0, MEM_RELEASE);
        image = nullptr;
    }

    bool send(UINT message, WPARAM value = 0, LPARAM data = 0,
              saves::BrowserInput input = saves::BrowserInput::keyboard) {
        return saves::browser_message(window, message, value, data, input);
    }

    void open(bool saving = true) {
        const auto before = pauses;
        require(saves::show_browser(saving) && saves::browser_active() && source,
                "Direct gameplay browser did not open");
        require(pauses == before + 1 && enhancements::scene_overlay_active(),
                "Gameplay browser did not acquire one native pause");
    }

    void confirm_load() {
        require(send(WM_KEYDOWN, VK_RETURN) && painted && painted->confirm,
                "Load action skipped its confirmation");
        require(send(WM_KEYDOWN, VK_RIGHT) && painted->focus == 11,
                "Cannot navigate to the confirmed load action");
        require(send(WM_KEYDOWN, VK_RETURN), "Confirmed load input escaped the browser");
    }

    void name() {
        for (unsigned i = 0; i < 8; ++i) {
            require(send(WM_KEYDOWN, VK_TAB), "Name navigation escaped the browser");
        }
        require(painted->focus == 9 && send(WM_KEYDOWN, VK_RETURN) && painted->keyboard &&
                    painted->naming && send(WM_KEYUP, VK_RETURN),
                "Cannot open the save-name keyboard");
    }
};

void check_name_input(Fixture& fixture) {
    fixture.open();
    fixture.name();
    require(fixture.send(WM_CHAR, L'A') && fixture.send(WM_CHAR, L'B') &&
                fixture.send(WM_CHAR, VK_BACK) && painted->name == L"A",
            "Physical typing or Backspace changed on the name keyboard");
    require(fixture.send(WM_KEYDOWN, VK_RIGHT) && painted->key == 1,
            "Cannot select an onscreen character");
    require(fixture.send(WM_KEYDOWN, VK_RETURN, 0, saves::BrowserInput::controller) &&
                painted->name == L"A2" && painted->keyboard && painted->naming,
            "Controller A did not append the selected onscreen character");
    require(fixture.send(WM_KEYDOWN, VK_RETURN) && painted->name == L"A2" && !painted->keyboard &&
                !painted->naming && !painted->confirm && painted->key == saves::name_key_count - 1,
            "Physical Enter did not select Done without changing the save name");
    require(fixture.send(WM_CHAR, VK_RETURN) && fixture.send(WM_KEYDOWN, VK_TAB) &&
                fixture.send(WM_KEYDOWN, VK_TAB) && painted->focus == 11,
            "Cannot navigate to Save after selecting Done");
    require(fixture.send(WM_KEYDOWN, VK_RETURN, 1L << 30) && !painted->confirm &&
                painted->name == L"A2" && fixture.send(WM_KEYUP, VK_RETURN),
            "Held physical Enter activated Save after leaving naming");
    require(fixture.send(WM_KEYDOWN, VK_RETURN) && painted->confirm,
            "A later physical Enter did not retain the overwrite confirmation");
    require(fixture.send(WM_KEYDOWN, VK_ESCAPE) && fixture.send(WM_KEYDOWN, VK_ESCAPE) &&
                !saves::browser_active(),
            "Cannot cancel the save-name input test");

    fixture.open();
    fixture.name();
    require(fixture.send(WM_CHAR, L'Z') && fixture.send(WM_KEYDOWN, VK_ESCAPE) &&
                !painted->keyboard && !painted->naming && painted->name == L"Z" &&
                saves::browser_active(),
            "Escape no longer dismisses naming while retaining the draft");
    require(fixture.send(WM_KEYDOWN, VK_RETURN) && painted->keyboard && painted->naming,
            "Cannot reopen the name keyboard after Escape");
    for (unsigned i = 0; i < saves::name_key_count - 1; ++i) {
        fixture.send(WM_KEYDOWN, VK_TAB, 0, saves::BrowserInput::controller);
    }
    require(fixture.send(WM_KEYDOWN, VK_RETURN, 0, saves::BrowserInput::controller) &&
                !painted->keyboard && !painted->naming && painted->name == L"Z",
            "Controller A did not retain the onscreen Done action");
    require(fixture.send(WM_KEYDOWN, VK_RETURN) && painted->keyboard && painted->naming &&
                fixture.send(WM_KEYDOWN, VK_RETURN) && !painted->keyboard &&
                fixture.send(WM_KEYDOWN, VK_ESCAPE) && !saves::browser_active(),
            "Cannot close the browser after physical Done");
    require(fixture.send(WM_CHAR, VK_RETURN) && fixture.send(WM_KEYDOWN, VK_RETURN, 1L << 30) &&
                fixture.send(WM_KEYUP, VK_RETURN) && !fixture.send(WM_CHAR, VK_RETURN) &&
                !fixture.send(WM_KEYDOWN, VK_RETURN),
            "Done input pairing leaked after close or consumed a later unrelated Enter");
}
}

const Settings& settings() {
    return options;
}

void trace_value(const char*, std::uint32_t) {}

namespace enhancements {
bool checkpoint_available() {
    return true;
}

bool export_save_available() {
    return true;
}

bool checkpoint_load_pending() {
    return pending;
}

void load_checkpoint(HWND, const std::filesystem::path& path) {
    require(path == L"synthetic-save.x", "Browser loaded a different selected save");
    ++loads;
    if (fail_load) {
        throw std::runtime_error("Synthetic load failure");
    }
    pending = true;
}

void export_save(const std::filesystem::path&) {
    throw std::runtime_error("Unexpected save publication");
}

void suspend_controller() {
    ++suspensions;
}
}

namespace enhancements::game {
const Edition& edition() {
    return profile;
}

std::byte* executable_image() {
    return image;
}

std::uintptr_t input_vtable() {
    return menu ? profile.main_menu : 0;
}

bool menu_confirmation_active() {
    return false;
}

bool saving_available() {
    return true;
}

ScriptControls script_controls() {
    return {};
}

RECT scene_bounds() {
    return {0, 0, 640, 480};
}
}

namespace native_game {
bool native_render_available() {
    return true;
}

void set_canvas_source(CanvasSource callback) {
    source = callback;
}

HDC canvas_dc() {
    return nullptr;
}

void invalidate_canvas() {}
}

namespace playback {
std::vector<MovieSnapshot> inspect_movies() {
    return {};
}
}

namespace media {
Movie::Movie(std::vector<std::uint8_t> data) : data_(std::move(data)) {}

struct Video::State {};

Video::Video() = default;
Video::~Video() = default;
}

namespace saves {
Canvas::Canvas() : dc(CreateCompatibleDC(nullptr)) {
    require(dc != nullptr, "Cannot create headless browser canvas");
}

Canvas::~Canvas() {
    DeleteDC(dc);
}

void draw_browser(Browser& state) {
    painted = &state;
}

void load_browser_art(Browser&) {}

void load_browser_page(Browser& state) {
    for (auto& slot : state.slots) {
        slot.file = L"synthetic-save.x";
        slot.occupied = slot.readable = true;
    }
}

Catalog read_catalog(const std::filesystem::path&) {
    return {};
}

BrowserText load_browser_text(const std::filesystem::path&) {
    BrowserText text;
    text.load_warning = L"Load this save?";
    return text;
}

Thumbnail capture_scene(HDC, const RECT&) {
    return {};
}

void delete_slot(const std::filesystem::path&, unsigned, SlotKind) {}

void write_slot(const std::filesystem::path&, unsigned, const std::wstring&,
                const std::filesystem::path&, const Thumbnail&, SlotKind) {}

Slot read_slot(const std::filesystem::path&, unsigned, SlotKind) {
    return {};
}

std::vector<Slot> read_autosaves(const std::filesystem::path&) {
    return {};
}

std::vector<Slot> read_quicksaves(const std::filesystem::path&) {
    return {};
}

void write_scene_reference(const std::filesystem::path&, const SceneReference&) {}

SceneReference read_scene_reference(const std::filesystem::path&) {
    return {};
}

bool valid_scene_reference(const SceneReference&) {
    return false;
}

ScenePreview::ScenePreview(const std::filesystem::path&, const SceneReference&)
    : movie_(std::vector<std::uint8_t>{}) {}

const media::Frame& ScenePreview::update(std::uint64_t) {
    static const media::Frame empty;
    return empty;
}
}

int main() {
    try {
        Fixture fixture;
        fixture.open();
        require(suspensions == 1 && !saves::show_browser(true) && pauses == 1,
                "An active browser acquired a second pause");
        require(fixture.send(WM_KEYDOWN, VK_ESCAPE) && !saves::browser_active() && !source &&
                    resumes == 1 && !enhancements::scene_overlay_active(),
                "Cancel did not resume the gameplay owner exactly once");
        saves::update_browser(fixture.window);
        require(resumes == 1, "Browser update resumed a canceled owner twice");

        fixture.open();
        require(fixture.send(WM_LBUTTONDOWN), "Browser leaked the cancel button press");
        require(fixture.send(WM_KEYDOWN, VK_ESCAPE) && !saves::browser_active() && resumes == 2,
                "Escape failed to close while the mouse was held");
        require(fixture.send(WM_LBUTTONUP), "Browser leaked the matching release after closing");
        require(!fixture.send(WM_LBUTTONUP), "Browser consumed an unrelated later release");

        fixture.open();
        require(fixture.send(WM_LBUTTONDOWN), "Browser leaked the focus-loss test press");
        fixture.send(WM_KILLFOCUS);
        require(fixture.send(WM_LBUTTONUP) && saves::browser_active() && resumes == 2,
                "A stale press canceled the browser after focus loss");
        require(fixture.send(WM_KEYDOWN, VK_ESCAPE) && resumes == 3,
                "Browser could not close after focus returned");

        fixture.open(false);
        fail_load = true;
        fixture.confirm_load();
        require(loads == 1 && saves::browser_active() && source && painted && !painted->confirm &&
                    painted->status == L"Synthetic load failure" &&
                    enhancements::scene_overlay_active() && resumes == 3,
                "Failed load discarded the browser or released its gameplay pause");
        require(fixture.send(WM_KEYDOWN, VK_ESCAPE) && resumes == 4,
                "Failed load could not be canceled back to gameplay");

        fixture.open(false);
        fail_load = false;
        fixture.confirm_load();
        require(loads == 2 && pending && application->state == &native_state &&
                    !saves::browser_active() && !source && !enhancements::scene_overlay_active() &&
                    resumes == 4,
                "Successful in-place load resumed the old scene's pause");
        pending = false;
        native_pause(application->state, 0, application_bytes.data() + 0x268);
        saves::release_browser();
        require(resumes == 5 && pauses == 5,
                "Browser cleanup interfered with the new scene owner's resume");
        check_name_input(fixture);
        require(resumes == 7 && pauses == 7,
                "Save naming interfered with the gameplay pause lifecycle");
        std::cout << "Browser pause, input pairing, save naming and load ownership passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
