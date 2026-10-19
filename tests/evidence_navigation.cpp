#include "enhancements/game_ui.h"
#include "enhancements/controller_state.h"
#include "settings.h"
#include <iostream>
#include <stdexcept>

namespace {
POINT pointer{};
ULONGLONG tick = 1000;
unsigned moves = 0, clicks = 0;
bool foreground = true;
Settings options;
enhancements::input::Frame test_frame;
const enhancements::game::Edition* test_profile = &enhancements::game::dvd;

BOOL WINAPI cursor_position(POINT* point) {
    *point = pointer;
    return TRUE;
}

BOOL WINAPI move_pointer(int x, int y) {
    pointer = {x, y};
    ++moves;
    return TRUE;
}

BOOL WINAPI coordinates(HWND, POINT*) {
    return TRUE;
}

BOOL WINAPI client_bounds(HWND, RECT* bounds) {
    *bounds = {0, 0, 640, 480};
    return TRUE;
}

UINT WINAPI submit_input(UINT count, INPUT* events, int) {
    for (UINT i = 0; i < count; ++i) {
        clicks += events[i].type == INPUT_MOUSE && (events[i].mi.dwFlags & MOUSEEVENTF_LEFTDOWN);
    }
    return count;
}

SHORT WINAPI key_state(int) {
    return 0;
}

ULONGLONG WINAPI time_now() {
    return tick;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
}

#define GetCursorPos cursor_position
#define SetCursorPos move_pointer
#define ScreenToClient coordinates
#define ClientToScreen coordinates
#define GetClientRect client_bounds
#define SendInput submit_input
#define GetAsyncKeyState key_state
#define GetTickCount64 time_now
#include "enhancements/controller.cpp"
#include "enhancements/dialogue.cpp"
#include "enhancements/inventory.cpp"
#undef GetCursorPos
#undef SetCursorPos
#undef ScreenToClient
#undef ClientToScreen
#undef GetClientRect
#undef SendInput
#undef GetAsyncKeyState
#undef GetTickCount64

bool ImportHooks::install(HMODULE, const char*, Resolver) {
    return false;
}

void ImportHooks::remove() {}

Identity identify(const std::filesystem::path&) {
    return {};
}

void trace_value(const char*, std::uint32_t) {}

const Settings& settings() {
    return options;
}

namespace transcript {
void record_choice(std::wstring) noexcept {}

bool active() {
    return false;
}

bool message(HWND, UINT, WPARAM, LPARAM) {
    return false;
}
}

namespace enhancements {
void set_game_string_code_page(UINT) noexcept {}

void set_game_string_dvd_fallback(bool) noexcept {}

int WINAPI load_game_string(HINSTANCE, UINT, LPSTR, int) noexcept {
    return 0;
}

bool game_is_foreground(HWND) {
    return foreground;
}

bool text_entry_busy() {
    return false;
}

bool controller_gun_busy() {
    return false;
}

bool equip_controller_gun() {
    return false;
}

void refresh_controller_gun_cursor(POINT) {}

void cancel_controller_inventory_click() {}

void begin_controller_inventory_click(HWND, POINT) {}

bool controller_inventory_click_pending() {
    return false;
}

bool navigate_emotions(HWND, int, bool) {
    return false;
}

bool navigate_screen(HWND, int, int, bool, bool, bool, bool, bool) {
    return false;
}

void update_rumble(unsigned, bool) noexcept {}

void stop_rumble() noexcept {}

bool point_controller(HWND, const RECT& bounds, bool, bool) {
    return move_pointer((bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2);
}

int hotspot_target(std::span<const RECT>, POINT, int, int) {
    return -1;
}

namespace reveal {
void suspend() {}

void controller(bool) {}
}

namespace documents {
bool active() {
    return false;
}

bool message(HWND, UINT, WPARAM, LPARAM) {
    return false;
}
}

namespace quick_menu {
bool expanded() {
    return false;
}
}

namespace input {
Frame poll(bool) {
    return test_frame;
}

Frame poll(bool enabled, const controller::Profile&) {
    return enabled ? test_frame : Frame{};
}
}

namespace game {
const Edition& edition() {
    return *test_profile;
}

std::byte* executable_image() {
    return nullptr;
}

void* current_input() {
    return nullptr;
}

std::uintptr_t input_vtable() {
    return 0;
}

bool menu_confirmation_active() {
    return false;
}

bool world_navigation_available() {
    return true;
}

RECT scene_bounds() {
    return {0, 0, 640, 410};
}

ScriptControls script_controls() {
    return {};
}

std::vector<RECT> emotion_targets() {
    return {};
}

std::vector<RECT> world_hotspots(bool, bool) {
    return {};
}

std::vector<RECT> aiming_targets() {
    return {};
}

bool movie_skippable() {
    return false;
}
}
}

namespace saves {
bool browser_active() {
    return false;
}

bool browser_message(HWND, UINT, WPARAM, LPARAM, BrowserInput) {
    return false;
}
}

namespace {
using namespace enhancements;

struct Fixture {
    game::MainView view{};
    game::Application app{};
    game::Application* app_pointer = &app;
    DrawList slot = nullptr;
    std::array<game::InventoryIcon, 5> icons{};
    std::array<game::InventoryItem, 5> items{};
    std::array<game::List<game::InventoryItem>::Node, 5> nodes{};
    std::array<game::ChildView, 5> children{};
    std::array<game::List<game::ChildView>::Node, 5> child_nodes{};

    Fixture() {
        clear_inventory_focus();
        clear_dialogue();
        reset_navigation();
        context_barrier.reset();
        options = {};
        options.analog_cursor = false;
        app.view = &view;
        application = &app_pointer;
        draw_slot = &slot;
        icons[0].rectangle.bounds = {40, 420, 80, 460};
        icons[1].rectangle.bounds = {300, 420, 340, 460};
        icons[2].rectangle.bounds = {300, 355, 340, 390};
        icons[3].rectangle.bounds = {300, 310, 340, 345};
        icons[4].rectangle.bounds = {300, 265, 340, 300};
        for (std::size_t i = 0; i < items.size(); ++i) {
            items[i].icon = &icons[i];
            nodes[i].value = &items[i];
        }
        nodes[0].next = &nodes[1];
        nodes[2].next = &nodes[3];
        nodes[3].next = &nodes[4];
        view.inventory.items.count = 2;
        view.inventory.items.first = &nodes[0];
        view.evidence.items.count = 3;
        view.evidence.items.first = &nodes[2];
        children[0].object = &view.inventory;
        children[1].object = &view.evidence;
        for (std::size_t i = 0; i < children.size(); ++i) {
            child_nodes[i].value = &children[i];
            child_nodes[i].next = i + 1 < children.size() ? &child_nodes[i + 1] : nullptr;
        }
        child_nodes[1].next = nullptr;
        auto& list = *reinterpret_cast<game::List<game::ChildView>*>(
            reinterpret_cast<std::byte*>(&view) + test_profile->children);
        list.count = 2;
        list.first = &child_nodes[0];
        pointer = {320, 210};
        test_frame = {};
        test_frame.connected = true;
        test_frame.player = 0;
    }

    void dialogue() {
        visible_container = &view.dialogue;
        view.dialogue.count = 1;
        visible = {};
        visible.choices[0] = {235, 400, 450, 416};
        visible.choices[1] = {235, 422, 450, 438};
        visible.count = 2;
        visible.talk = {230, 375, 320, 391};
        visible.history = {330, 375, 430, 391};
    }

    void at(unsigned index) {
        const auto& r = icons[index].rectangle.bounds;
        pointer = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    }

    void controller(WORD buttons) {
        test_frame.sample.buttons = test_frame.pressed = buttons;
        ++tick;
        poll_controller(nullptr);
        test_frame.sample.buttons = test_frame.pressed = 0;
        ++tick;
        poll_controller(nullptr);
    }
};

void selected(const RECT& bounds, const char* message) {
    require(PtInRect(&bounds, pointer), message);
}

void check_inventory() {
    Fixture fixture;
    require(focus_inventory(nullptr), "Cannot focus primary inventory");
    selected(fixture.icons[0].rectangle.bounds, "Initial focus changed from the first base item");
    fixture.icons[0].rectangle.bounds = {250, 414, 290, 460};
    fixture.at(1);
    fixture.controller(input::button::up);
    selected(fixture.icons[2].rectangle.bounds, "Actual controller Up remained on Evidence kit");
    test_frame.sample.buttons = input::button::up;
    test_frame.pressed = 0;
    tick += 500;
    poll_controller(nullptr);
    selected(fixture.icons[2].rectangle.bounds, "A held controller key skipped an evidence item");
    test_frame.sample.buttons = 0;
    ++tick;
    poll_controller(nullptr);
    fixture.controller(input::button::up);
    selected(fixture.icons[3].rectangle.bounds,
             "Controller Up did not follow the vertical evidence row");
    fixture.controller(input::button::up);
    selected(fixture.icons[4].rectangle.bounds, "Controller Up missed the uppermost evidence");
    fixture.controller(input::button::up);
    selected(fixture.icons[4].rectangle.bounds,
             "Vertical inventory navigation wrapped at the upper edge");
    fixture.controller(input::button::down);
    fixture.controller(input::button::down);
    fixture.controller(input::button::down);
    selected(fixture.icons[1].rectangle.bounds,
             "Controller Down did not return from evidence to the kit");
    fixture.controller(input::button::left);
    selected(fixture.icons[0].rectangle.bounds, "Inventory Left changed rows");
    fixture.controller(input::button::left);
    selected(fixture.icons[1].rectangle.bounds, "Inventory horizontal cycling was lost");
    fixture.nodes[2].next = &fixture.nodes[4];
    fixture.nodes[4].next = &fixture.nodes[3];
    fixture.nodes[3].next = nullptr;
    fixture.controller(input::button::up);
    selected(fixture.icons[2].rectangle.bounds,
             "Reordered ownership changed geometric evidence navigation");
    fixture.at(3);
    fixture.child_nodes[0].next = nullptr;
    auto& children = *reinterpret_cast<game::List<game::ChildView>*>(
        reinterpret_cast<std::byte*>(&fixture.view) + test_profile->children);
    children.count = 1;
    require(!inventory_focused(nullptr), "Removed evidence retained inventory focus");
    require(!navigate_inventory(nullptr, 0, -1), "Hidden evidence remained navigable");
    pointer = {320, 210};
    require(focus_inventory(nullptr), "Cannot focus inventory before native view replacement");
    game::MainView replacement{};
    fixture.app.view = &replacement;
    require(!inventory_focused(nullptr), "A replaced native view retained old inventory focus");
}

void check_primary_popup() {
    Fixture fixture;
    fixture.nodes[1].next = &fixture.nodes[2];
    fixture.view.inventory.items.count = 5;
    fixture.view.evidence.items = {};
    require(focus_inventory(nullptr), "Cannot focus inventory with primary evidence popup");
    fixture.at(1);
    for (const auto index : {2u, 3u, 4u}) {
        fixture.controller(input::button::up);
        selected(fixture.icons[index].rectangle.bounds,
                 "Primary inventory popup Up did not reach each evidence item");
    }
    for (const auto index : {3u, 2u, 1u}) {
        fixture.controller(input::button::down);
        selected(fixture.icons[index].rectangle.bounds,
                 "Primary inventory popup Down did not return through the stack");
    }
    fixture.at(2);
    fixture.nodes[1].next = nullptr;
    fixture.view.inventory.items.count = 2;
    require(!inventory_focused(nullptr), "Closed primary popup retained evidence focus");
}

void check_groups() {
    Fixture fixture;
    auto& children = *reinterpret_cast<game::List<game::ChildView>*>(
        reinterpret_cast<std::byte*>(&fixture.view) + test_profile->children);
    std::array<game::List<game::InventoryItem>::Node, 3> conversation_nodes{};
    for (unsigned i = 0; i < 3; ++i) {
        conversation_nodes[i].value = &fixture.items[i == 2 ? 3 : i];
        fixture.children[i + 2].object = &fixture.view.conversation_icons[i].inventory;
        auto& group = fixture.view.conversation_icons[i].inventory;
        group.items.count = 1;
        group.items.first = &conversation_nodes[i];
    }
    fixture.child_nodes[1].next = &fixture.child_nodes[2];
    children.count = 5;
    require(evidence_bounds(&fixture.view).size() == 5,
            "One of four visible native evidence groups was omitted");
    require(inventory_items(&fixture.view).size() == 2,
            "Evidence collector changed primary inventory ownership or asset identity");
    game::Inventory unowned = fixture.view.evidence;
    fixture.children[1].object = &unowned;
    require(evidence_bounds(&fixture.view).size() == 3 &&
                inventory_bounds(&fixture.view).size() == 2,
            "A populated unowned evidence group was accepted as visible");
    fixture.children[1].object = &fixture.view.evidence;
    fixture.child_nodes[3].value = reinterpret_cast<game::ChildView*>(1);
    require(evidence_bounds(&fixture.view).empty() && inventory_bounds(&fixture.view).empty(),
            "An unreadable child membership chain was dereferenced or partially accepted");
    fixture.child_nodes[3].value = &fixture.children[3];
    fixture.nodes[2].next = reinterpret_cast<game::List<game::InventoryItem>::Node*>(1);
    require(evidence_bounds(&fixture.view).size() == 3,
            "A malformed evidence node chain exposed a partial hidden row");
    fixture.nodes[2].next = &fixture.nodes[3];
    fixture.nodes[4].next = &fixture.nodes[2];
    require(evidence_bounds(&fixture.view).size() == 3,
            "A cyclic evidence node chain was accepted");
    fixture.nodes[4].next = nullptr;
    fixture.icons[2].rectangle.bounds.right = 700;
    require(evidence_bounds(&fixture.view).size() == 4, "Offscreen evidence bounds were accepted");
    fixture.icons[2].rectangle.bounds.right = 340;
    fixture.items[2].icon = reinterpret_cast<game::InventoryIcon*>(1);
    require(evidence_bounds(&fixture.view).size() == 4,
            "Unreadable evidence icon was dereferenced or discarded valid siblings");
    fixture.items[2].icon = &fixture.icons[2];
    fixture.view.evidence.items.count = 65;
    require(evidence_bounds(&fixture.view).size() == 3, "An oversized evidence list was accepted");
    fixture.view.evidence.items.count = 3;
    fixture.child_nodes[4].next = &fixture.child_nodes[0];
    require(evidence_bounds(&fixture.view).empty(), "A cyclic visible child list was accepted");
}

void check_dialogue() {
    Fixture fixture;
    fixture.dialogue();
    pointer = {340, 408};
    fixture.controller(0);
    fixture.controller(input::button::up);
    selected(fixture.icons[2].rectangle.bounds, "First-response Up did not enter evidence");
    const auto before = clicks;
    fixture.controller(input::button::right);
    selected(fixture.icons[2].rectangle.bounds, "Evidence Right switched native dialogue tabs");
    require(clicks == before, "Evidence navigation activated a Talk or History tab");
    fixture.controller(input::button::down);
    selected(visible.choices[0], "Evidence Down did not return to a dialogue response");
    fixture.controller(input::button::evidence);
    selected(fixture.icons[4].rectangle.bounds, "R3 compatibility did not focus evidence");
    fixture.controller(input::button::evidence);
    selected(visible.choices[0], "R3 roundtrip did not return to choices");
    fixture.controller(input::button::right);
    require(clicks == before + 1, "Dialogue Right no longer selects the History tab");
    fixture.icons[3].rectangle.bounds = {360, 349, 400, 392};
    fixture.icons[4].rectangle.bounds = {330, 360, 350, 398};
    pointer = {340, 408};
    fixture.controller(input::button::up);
    selected(fixture.icons[4].rectangle.bounds,
             "Response Up did not choose nearest unequal-height evidence");
    fixture.at(2);
    fixture.controller(input::button::down);
    selected(visible.choices[0], "Evidence Down moved sideways through a taller same-row icon");
    pointer = {320, 372};
    fixture.controller(input::button::up);
    selected(fixture.icons[2].rectangle.bounds,
             "Evidence Up moved sideways through a taller same-row icon");
    fixture.at(1);
    clear_dialogue();
    require(focus_inventory(nullptr), "Cannot prepare stale inventory focus fixture");
    fixture.at(2);
    fixture.dialogue();
    fixture.controller(0);
    fixture.controller(input::button::down);
    selected(visible.choices[0], "Stale inventory focus captured dialogue evidence Down");
}
}

int main() {
    try {
        for (const auto edition : {&game::cd, &native_game::profile_cd_10019,
                                   &native_game::profile_cd_10020, &game::dvd}) {
            test_profile = edition;
            check_inventory();
            check_primary_popup();
            check_groups();
            check_dialogue();
        }
        require(moves > 0, "Navigation fixture never exercised production pointer movement");
        std::cout << "Production evidence inventory, dialogue and controller routing passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
