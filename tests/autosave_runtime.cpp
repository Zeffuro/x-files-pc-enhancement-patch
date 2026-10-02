#include "enhancements/autosave.h"
#include "enhancements/quick_save.h"
#include "enhancements/game_ui.h"
#include "enhancements/dialogue.h"
#include "enhancements/scene_overlay.h"
#include "saves/browser.h"
#include "saves/recent.h"
#include "saves/header.h"
#include "platform/copy_file.h"
#include "playback/inspection.h"
#include "settings.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::byte* image = nullptr;
enhancements::game::Edition profile = enhancements::game::cd;
alignas(enhancements::game::Application) std::array<std::byte, 0x300> app_bytes{};
auto* fixture_app = reinterpret_cast<enhancements::game::Application*>(app_bytes.data());
enhancements::game::MainView view{};
std::byte fixture_scene{}, another_scene{};
Settings fixture_options;
fs::path fixture_root;
ULONGLONG tick = 1000;
bool world = true, dialogue = false, emotion = false, overlay = false, browser = false;
bool fixture_menu = false, menu_confirm = false, saving = true, invalid_native = false;
bool fail_publish = false, flip_before_export = false, native_contract = true;
enhancements::game::ScriptControls controls;
std::vector<playback::MovieSnapshot> movies;
std::string native_name;
std::wstring notification;
unsigned serialized = 0, published = 0, previews = 0, quick_records = 0, dialogs = 0;
unsigned native_loads = 0;

std::string bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void save_fixture(const fs::path& path, const char* payload) {
    std::array<char, 24> header{};
    header[3] = 5;
    header[22] = 5;
    header[23] = 1;
    std::ofstream output(path, std::ios::binary);
    output.write(header.data(), header.size());
    output << payload;
}

ULONGLONG test_tick() {
    return tick;
}

DWORD test_module_name(HMODULE, LPWSTR buffer, DWORD size) {
    const auto name = (fixture_root / L"synthetic.exe").wstring();
    if (name.size() >= size) {
        return 0;
    }
    std::copy(name.begin(), name.end(), buffer);
    buffer[name.size()] = L'\0';
    if (flip_before_export) {
        dialogue = true;
    }
    return static_cast<DWORD>(name.size());
}

int test_message_w(HWND, LPCWSTR, LPCWSTR, UINT) {
    ++dialogs;
    return IDNO;
}

int test_message_a(HWND, LPCSTR, LPCSTR, UINT) {
    ++dialogs;
    return IDNO;
}

void __fastcall native_create(void*, void*, const char* name) {
    native_name = name;
}

void __fastcall native_destroy(void*, void*) {
    native_name.clear();
}

void __stdcall native_state(void* queue, void* unused) {
    native_contract = native_contract && queue == app_bytes.data() + 0x268 && !unused &&
                      !enhancements::safe_save_available();
}

void __stdcall native_file(void*, void* queue) {
    native_contract = native_contract && queue == app_bytes.data() + 0x268;
    ++serialized;
    if (invalid_native) {
        std::ofstream(native_name) << "incomplete native output";
    } else {
        save_fixture(native_name, ("serialization " + std::to_string(serialized)).c_str());
    }
}

int __stdcall native_load(void*, void* queue) {
    native_contract =
        native_contract && queue == app_bytes.data() + 0x268 && native_name == "saves\\QUICKSAVE.x";
    ++native_loads;
    return 1;
}

template <typename Callback> void trampoline(std::uint32_t address, Callback callback) {
    static_assert(sizeof(callback) == 4);
    image[address] = std::byte{0x68};
    std::memcpy(image + address + 1, &callback, sizeof(callback));
    image[address + 5] = std::byte{0xc3};
}
}

// Compile the production orchestration with a deterministic clock and headless OS adapters.
#define GetTickCount64 test_tick
#define GetModuleFileNameW test_module_name
#define MessageBoxW test_message_w
#define MessageBoxA test_message_a
#include "../src/enhancements/autosave.cpp"
#include "../src/enhancements/quick_save.cpp"
#undef MessageBoxA
#undef MessageBoxW
#undef GetModuleFileNameW
#undef GetTickCount64

const Settings& settings() {
    return fixture_options;
}

void trace_value(const char*, std::uint32_t) {}

namespace enhancements {
const Dialogue* current_dialogue() {
    static Dialogue value;
    return dialogue ? &value : nullptr;
}

bool scene_overlay_active() {
    return overlay;
}

bool scene_overlay_checkpoint_available() {
    return overlay;
}

void notify_status(HWND, const wchar_t* message) {
    notification = message;
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
    return fixture_menu ? profile.main_menu : 0;
}

bool menu_confirmation_active() {
    return menu_confirm;
}

bool saving_available() {
    return saving;
}

bool world_navigation_available() {
    return world && !fixture_menu;
}

std::vector<RECT> emotion_targets() {
    return emotion ? std::vector<RECT>{{}} : std::vector<RECT>{};
}

ScriptControls script_controls() {
    return controls;
}

RECT scene_bounds() {
    return {0, 0, 640, 480};
}
}

namespace playback {
std::vector<MovieSnapshot> inspect_movies() {
    return movies;
}
}

namespace transcript {
void record_marker(std::wstring) noexcept {}
}

namespace saves {
bool browser_active() {
    return browser;
}

Thumbnail browser_scene_thumbnail() {
    return {1, 1, {1, 2, 3, 4}};
}

SceneReference browser_scene_reference() {
    return {L"navm.nmv", 1, 3, false};
}

void write_scene_reference(const fs::path&, const SceneReference& reference) {
    require(reference.movie == L"navm.nmv" && reference.sample == 3,
            "Autosave lost the captured fixture_scene reference");
    ++previews;
}

Slot write_autosave(const fs::path& game, const fs::path& prepared, const Thumbnail& thumbnail) {
    require(game == fixture_root && supported_header(prepared) && thumbnail.pixels.size() == 4,
            "Autosave published invalid native data or lost the fixture_scene capture");
    if (fail_publish) {
        throw std::runtime_error("Synthetic publication failure");
    }
    const auto destination = fixture_root / L"saves" / L"published.x";
    platform::copy_file(prepared, destination, fs::copy_options::overwrite_existing);
    ++published;
    return {published, L"Auto", L"date", destination, {}, true, true};
}

void record_quicksave(const fs::path& game) {
    require(game == fixture_root, "Quick-save metadata used a different game fixture_root");
    ++quick_records;
}
}

namespace {
struct Fixture {
    fs::path previous = fs::current_path();

    Fixture() {
        fixture_root = fs::temp_directory_path() /
                       (L"xfiles-autosave-runtime-" + std::to_wstring(GetCurrentProcessId()) +
                        L"-" + std::to_wstring(GetTickCount64()));
        fs::create_directory(fixture_root);
        fs::current_path(fixture_root);
        image = static_cast<std::byte*>(VirtualAlloc(
            nullptr, profile.application + 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        require(image != nullptr, "Cannot allocate the synthetic executable image");
        profile.pending_load = 0x108;
        profile.string_create = 0x200;
        profile.string_destroy = 0x210;
        profile.save_state = 0x220;
        profile.save_file = 0x230;
        profile.load_file = 0x240;
        fixture_app->state = &fixture_scene;
        fixture_app->view = &view;
        std::memcpy(image + profile.application, &fixture_app, sizeof(fixture_app));
        trampoline(profile.string_create, native_create);
        trampoline(profile.string_destroy, native_destroy);
        trampoline(profile.save_state, native_state);
        trampoline(profile.save_file, native_file);
        trampoline(profile.load_file, native_load);
        FlushInstructionCache(GetCurrentProcess(), image, 4096);
        fixture_options.autosaves = true;
        playback::MovieSnapshot still{};
        still.path = L"navm.nmv";
        still.active = still.video = true;
        still.width = 640;
        still.height = 480;
        still.image = media::FrameReference{1, 3, 10};
        movies = {still};
    }

    ~Fixture() {
        enhancements::release_autosave();
        VirtualFree(image, 0, MEM_RELEASE);
        image = nullptr;
        fs::current_path(previous);
    }
};

void stable_update(bool blocked = false) {
    tick += 40000;
    enhancements::update_autosave(nullptr, blocked);
    tick += 2000;
    enhancements::update_autosave(nullptr, blocked);
}

template <typename Set> void rejects_updates(Set set, const char* message, bool blocked = false) {
    enhancements::release_autosave();
    const auto before = serialized;
    set(true);
    stable_update(blocked);
    set(false);
    require(serialized == before, message);
}

void rejects_export(const fs::path& destination) {
    bool rejected = false;
    const auto before = serialized;
    try {
        enhancements::export_safe_save(destination);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && serialized == before && !fs::exists(destination),
            "Unsafe safe-export reached the native serializer or published output");
}

void check_reparse_directory() {
    const auto regular = fixture_root / L"regular-saves";
    const auto target = fixture_root / L"redirected-saves";
    const auto link = fixture_root / L"saves";
    fs::create_directory(target);
    fs::rename(link, regular);
    constexpr DWORD allow_unprivileged = 0x2;
    if (!CreateSymbolicLinkW(link.c_str(), target.c_str(),
                             SYMBOLIC_LINK_FLAG_DIRECTORY | allow_unprivileged)) {
        const auto error = GetLastError();
        fs::rename(regular, link);
        std::cout
            << "Reparse save-directory check skipped: unprivileged directory links unavailable ("
            << error << ").\n";
        return;
    }
    const auto attributes = GetFileAttributesW(link.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        require(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND,
                "Cannot inspect the synthetic directory link");
        fs::rename(regular, link);
        std::cout << "Reparse save-directory check skipped: platform did not create the link.\n";
        return;
    }
    if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        require(RemoveDirectoryW(link.c_str()) != FALSE,
                "Cannot remove the synthetic directory link");
        fs::rename(regular, link);
        std::cout << "Reparse save-directory check skipped: platform does not expose reparse "
                     "attributes.\n";
        return;
    }
    const auto before = serialized;
    rejects_export(fixture_root / L"redirected-export.x");
    enhancements::quick_save(nullptr, false);
    require(serialized == before && fs::is_empty(target),
            "Reparse saves directory reached native serialization or received output");
    require(RemoveDirectoryW(link.c_str()) != FALSE, "Cannot remove the synthetic directory link");
    fs::rename(regular, link);
}
}

int main() {
    try {
        Fixture fixture;
        require(enhancements::safe_save_available(), "Safe exploration was unavailable");
        stable_update();
        require(serialized == 1 && published == 1 && previews == 1 &&
                    notification == L"Autosave complete" && native_contract,
                "Autosave did not serialize, publish and announce the stable native fixture_scene");
        stable_update();
        require(serialized == 1, "Unchanged exploration repeatedly autosaved");
        movies[0].image->sample = 4;
        stable_update();
        require(serialized == 2 && published == 2,
                "A navigation-frame transition did not autosave");
        fixture_app->state = &another_scene;
        stable_update();
        require(serialized == 3 && published == 3,
                "A native fixture_scene transition did not autosave");

        rejects_updates([](bool on) { dialogue = on; }, "Dialogue reached native autosave");
        rejects_updates([](bool on) { emotion = on; }, "Emotion choices reached native autosave");
        rejects_updates(
            [](bool on) { controls.buttons = on ? std::vector<RECT>{{}} : std::vector<RECT>{}; },
            "Script buttons reached native autosave");
        rejects_updates([](bool on) { controls.script_dialog = on; },
                        "Script dialog reached native autosave");
        rejects_updates([](bool on) { controls.text_input = on; },
                        "Text input reached native autosave");
        rejects_updates([](bool on) { overlay = on; }, "Paused overlay reached native autosave");
        rejects_updates([](bool on) { browser = on; }, "Save browser reached native autosave");
        rejects_updates([](bool on) { fixture_menu = on; },
                        "Main fixture_menu reached native autosave");
        rejects_updates([](bool on) { menu_confirm = on; },
                        "Menu confirmation reached native autosave");
        rejects_updates(
            [](bool on) {
                *reinterpret_cast<void**>(image + profile.pending_load) =
                    on ? &fixture_scene : nullptr;
            },
            "Pending native load reached autosave");
        rejects_updates([](bool on) { fixture_options.autosaves = !on; },
                        "Disabled autosaves wrote native state");
        rejects_updates([](bool) {}, "Background suspension reached native autosave", true);
        rejects_updates([](bool on) { saving = !on; },
                        "Unavailable native saving reached autosave");
        rejects_updates([](bool on) { world = !on; }, "Non-exploration reached native autosave");
        rejects_updates(
            [](bool on) {
                movies[0].playing = on;
                movies[0].path = on ? L"XV/39044.xmv" : L"navm.nmv";
            },
            "Playing fixture_scene movie reached native autosave");
        rejects_updates([](bool on) { fixture_app->view = on ? nullptr : &view; },
                        "Missing native view reached autosave");

        dialogue = true;
        rejects_export(fixture_root / L"unsafe.x");
        dialogue = false;
        enhancements::release_autosave();
        flip_before_export = true;
        const auto before_recheck = serialized;
        stable_update();
        flip_before_export = dialogue = false;
        require(serialized == before_recheck &&
                    notification == L"Autosave failed. Existing saves kept",
                "Autosave failed to recheck safety immediately before native export");

        const auto previous_auto = bytes(fixture_root / L"saves" / L"published.x");
        invalid_native = true;
        enhancements::release_autosave();
        stable_update();
        require(bytes(fixture_root / L"saves" / L"published.x") == previous_auto &&
                    published == 3 && notification == L"Autosave failed. Existing saves kept",
                "Invalid native serialization replaced the published autosave");
        invalid_native = false;
        fail_publish = true;
        enhancements::release_autosave();
        stable_update();
        fail_publish = false;
        require(bytes(fixture_root / L"saves" / L"published.x") == previous_auto && published == 3,
                "Failed publication discarded the previous autosave");
        for (const auto& entry : fs::directory_iterator(fixture_root / L"saves")) {
            require(!entry.path().filename().wstring().starts_with(L"AUTOSAVE.pending.") &&
                        !entry.path().filename().wstring().starts_with(L"EXPORT."),
                    "Failed autosave retained an internal temporary file");
        }

        enhancements::quick_save(nullptr, false);
        const auto first_quick = bytes(fixture_root / L"saves" / L"QUICKSAVE.x");
        require(!first_quick.empty() && quick_records == 1,
                "Quick-save did not publish native output");
        enhancements::quick_save(nullptr, false);
        require(bytes(fixture_root / L"saves" / L"QUICKSAVE.previous.x") == first_quick &&
                    quick_records == 2,
                "Quick-save did not preserve its previous generation");
        const auto current_quick = bytes(fixture_root / L"saves" / L"QUICKSAVE.x");
        invalid_native = true;
        enhancements::quick_save(nullptr, false);
        require(
            bytes(fixture_root / L"saves" / L"QUICKSAVE.x") == current_quick &&
                bytes(fixture_root / L"saves" / L"QUICKSAVE.previous.x") == first_quick &&
                quick_records == 2 && dialogs == 1 && native_contract,
            "Invalid quick-save replaced a good generation or violated the native queue contract");
        invalid_native = false;
        enhancements::release_autosave();
        enhancements::quick_save(nullptr, true);
        require(native_loads == 1 && native_contract && !enhancements::checkpoint_load_pending(),
                "Ordinary gameplay quick-load did not use the native queue and quick-save path");
        const auto load_tick = tick;
        const auto before_load_grace = serialized;
        ++movies[0].image->sample;
        enhancements::update_autosave(nullptr, false);
        tick = load_tick + 2000;
        enhancements::update_autosave(nullptr, false);
        require(serialized == before_load_grace,
                "Ordinary gameplay quick-load failed to suppress autosaving during the load grace");
        tick = load_tick + 30000;
        enhancements::update_autosave(nullptr, false);
        require(serialized == before_load_grace + 1 && published == 4,
                "Stable exploration did not autosave after the ordinary quick-load grace expired");
        enhancements::release_autosave();
        movies[0].path = L"XN/56182.xmv";
        movies[0].playing = true;
        const auto before_loop = serialized;
        tick += 40000;
        enhancements::update_autosave(nullptr, false);
        for (unsigned i = 0; i < 20; ++i) {
            tick += 100;
            ++movies[0].image->sample;
            enhancements::update_autosave(nullptr, false);
        }
        require(serialized == before_loop + 1 && published == 5,
                "Ambient navigation frames prevented a stable exploration autosave");
        for (unsigned i = 0; i < 40; ++i) {
            tick += 1000;
            ++movies[0].image->sample;
            enhancements::update_autosave(nullptr, false);
        }
        require(serialized == before_loop + 1,
                "An ambient navigation loop repeatedly autosaved its changing frames");
        rejects_updates([](bool on) { movies[0].path = on ? L"XV/56182.xmv" : L"XN/56182.xmv"; },
                        "A playing story movie bypassed the cutscene gate");
        check_reparse_directory();
        std::cout
            << "Autosave runtime gates, native export recheck and quick-save preservation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
