#include <windows.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
std::array<bool, 256> held_keys{};

SHORT WINAPI test_key_state(int key) {
    return held_keys.at(key) ? SHORT(-32768) : SHORT(0);
}
}

#define GetAsyncKeyState test_key_state
#include "enhancements/hotspot_reveal.cpp"
#undef GetAsyncKeyState

namespace {
Settings options;
bool foreground = true, paused = false, inventory = false, quick = false, safe = true;
const enhancements::Dialogue* dialogue = nullptr;
native_game::CanvasSource reveal_canvas = nullptr;
enhancements::game::MainView views[2]{};
unsigned view_index = 0;
unsigned redraws = 0;
std::uintptr_t native_input = 0;
enhancements::game::ScriptControls script;
std::vector<RECT> emotions;
std::vector<enhancements::game::WorldTarget> targets{
    {{200, 100, 240, 140}, {200, 100, 240, 140}, 17, false}};

void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

void shown(bool expected, const char* reason) {
    require((reveal_canvas != nullptr) == expected, reason);
}

void reset() {
    enhancements::reveal::release();
    held_keys.fill(false);
    foreground = safe = true;
    paused = inventory = quick = false;
    dialogue = nullptr;
    options = {};
    view_index = 0;
    native_input = 0;
    script = {};
    emotions.clear();
    targets[0].identity = 17;
    targets[0].interaction = enhancements::game::Interaction::unknown;
    enhancements::reveal::update(nullptr, true);
}

bool key(UINT event, WPARAM value, LPARAM data = 0) {
    return enhancements::reveal::message(nullptr, event, value, data);
}

void press(unsigned virtual_key) {
    held_keys[virtual_key] = true;
    require(key(WM_KEYDOWN, virtual_key), "Reveal press did not own its configured key");
}

void release(unsigned virtual_key) {
    held_keys[virtual_key] = false;
    require(key(WM_KEYUP, virtual_key), "Reveal release did not match its consumed press");
    shown(false, "Reveal remained after key release");
}

void keyboard() {
    using namespace enhancements;
    reset();
    held_keys[VK_LMENU] = true;
    require(key(WM_SYSKEYDOWN, VK_MENU), "Left Alt alias was not recognized");
    shown(true, "Left Alt did not reveal");
    require(key(WM_SYSKEYDOWN, VK_MENU, LPARAM{1} << 30), "Alt repeat leaked");
    held_keys[VK_LMENU] = false;
    require(key(WM_SYSKEYUP, VK_MENU), "Left Alt alias release leaked");
    shown(false, "Alt release retained markers");
    held_keys[VK_RMENU] = held_keys[VK_CONTROL] = true;
    require(!key(WM_SYSKEYDOWN, VK_MENU, LPARAM{1} << 24), "AltGr was consumed as left Alt");
    reveal::update(nullptr, true);
    shown(false, "AltGr revealed hotspots");
    held_keys[VK_RMENU] = held_keys[VK_CONTROL] = false;
    require(!key(WM_SYSKEYUP, VK_MENU, LPARAM{1} << 24), "AltGr release was consumed");

    for (const auto shortcut : {VK_RETURN, VK_TAB}) {
        held_keys[VK_LMENU] = true;
        require(key(WM_SYSKEYDOWN, VK_MENU), "Cannot start Alt shortcut fixture");
        shown(true, "Alt did not reveal before system shortcut");
        held_keys[shortcut] = true;
        require(!key(WM_SYSKEYDOWN, shortcut), "System shortcut was consumed");
        shown(false, "System shortcut retained reveal");
        held_keys[shortcut] = false;
        require(!key(WM_SYSKEYUP, shortcut), "System shortcut release was consumed");
        reveal::update(nullptr, true);
        shown(false, "Held Alt reappeared after system shortcut");
        held_keys[VK_LMENU] = false;
        require(key(WM_SYSKEYUP, VK_MENU), "Consumed Alt release leaked after shortcut");
    }

    for (const unsigned configured : {'H', 'R'}) {
        options.hotspot_reveal_key = configured;
        reveal::update(nullptr, true);
        press(configured);
        shown(true, "Configured letter did not reveal");
        require(key(WM_CHAR, configured + ('a' - 'A')) && key(WM_CHAR, configured),
                "Configured character leaked into native input");
        require(!key(WM_CHAR, 'x'), "An unrelated character was consumed");
        release(configured);
        require(!key(WM_CHAR, configured), "Released letter retained character ownership");
    }
}

void transitions() {
    using namespace enhancements;
    reset();
    options.hotspot_reveal_key = 'H';
    press('H');
    shown(true, "Cannot reveal before scene replacement");
    view_index = 1;
    reveal::update(nullptr, true);
    shown(true, "Held reveal did not cross a view replacement");
    reveal::update(nullptr, true);
    shown(true, "Stationary redraw lost a continuous hold");
    release('H');
    press('H');
    shown(true, "Fresh press did not rearm on new scene");
    ++targets[0].identity;
    reveal::update(nullptr, true);
    shown(true, "In-place scene target replacement lost reveal");
    release('H');

    for (const unsigned configured : {unsigned(VK_LMENU), unsigned('H'), unsigned('R')}) {
        options.hotspot_reveal_key = configured;
        press(configured);
        safe = false;
        native_input = game::edition().movie;
        reveal::update(nullptr, true);
        shown(false, "Reveal remained over a transition movie");
        view_index ^= 1;
        ++targets[0].identity;
        safe = true;
        native_input = 0;
        reveal::update(nullptr, true);
        shown(true, "Continuous key hold was lost after a transition movie");
        safe = false;
        reveal::update(nullptr, true);
        release(configured);
        safe = true;
        reveal::update(nullptr, true);
        shown(false, "Key released during transition revealed the next scene");
    }
    options.hotspot_reveal_key = 'H';

    press('H');
    safe = false;
    native_input = game::edition().movie;
    held_keys[VK_CONTROL] = true;
    reveal::update(nullptr, true);
    held_keys[VK_CONTROL] = false;
    safe = true;
    native_input = 0;
    reveal::update(nullptr, true);
    shown(false, "Modifier used during a movie failed to block the held key");
    release('H');

    for (int context = 0; context < 4; ++context) {
        press('H');
        reveal::controller(true);
        safe = false;
        script.script_dialog = context == 0;
        script.text_input = context == 1;
        if (context == 2) {
            script.buttons.push_back({10, 10, 30, 30});
        }
        if (context == 3) {
            emotions.push_back({10, 10, 30, 30});
        }
        reveal::update(nullptr, true);
        shown(false, "Reveal remained in a script or emotion context");
        safe = true;
        script = {};
        emotions.clear();
        reveal::update(nullptr, true);
        shown(false, "Held input returned after a script or emotion context");
        reveal::controller(false);
        release('H');
    }

    for (bool* blocker : {&paused, &inventory, &quick}) {
        press('H');
        shown(true, "Cannot reveal before overlay transition");
        *blocker = true;
        reveal::update(nullptr, true);
        shown(false, "Reveal remained over a patch overlay");
        *blocker = false;
        reveal::update(nullptr, true);
        shown(false, "Held reveal returned when overlay closed");
        release('H');
    }
    for (bool* eligibility : {&foreground}) {
        press('H');
        *eligibility = false;
        reveal::update(nullptr, true);
        shown(false, "Reveal remained outside safe foreground exploration");
        *eligibility = true;
        reveal::update(nullptr, true);
        shown(false, "Held reveal returned after focus or native context change");
        release('H');
    }
    for (const auto input : {std::uintptr_t{game::edition().main_menu}, std::uintptr_t{100}}) {
        press('H');
        safe = false;
        native_input = input;
        reveal::update(nullptr, true);
        shown(false, "Reveal remained in a native device or menu");
        safe = true;
        native_input = 0;
        reveal::update(nullptr, true);
        shown(false, "Held reveal returned after a native device or menu");
        release('H');
    }
    Dialogue talk;
    press('H');
    dialogue = &talk;
    reveal::update(nullptr, true);
    shown(false, "Reveal remained over dialogue");
    dialogue = nullptr;
    reveal::update(nullptr, true);
    shown(false, "Held reveal returned after dialogue");
    release('H');
    press('H');
    reveal::suspend();
    reveal::update(nullptr, true);
    shown(false, "Focus suspension failed its release barrier");
    release('H');
    press('H');
    shown(true, "Fresh key hold failed after focus suspension");
    options.hotspot_reveal = false;
    reveal::update(nullptr, true);
    shown(false, "Disabled reveal remained visible");
    options.hotspot_reveal = true;
    reveal::update(nullptr, true);
    shown(false, "Enabling reveal accepted a stale hold");
    release('H');
}

void controller_hold() {
    using namespace enhancements;
    reset();
    reveal::controller(true);
    reveal::update(nullptr, true);
    shown(true, "Controller Targets hold did not reveal");
    reveal::update(nullptr, true);
    shown(true, "Stationary controller hold was lost");
    view_index = 1;
    reveal::update(nullptr, true);
    shown(true, "Controller hold did not cross a scene change");
    safe = false;
    native_input = game::edition().action_movie;
    reveal::update(nullptr, true);
    shown(false, "Controller reveal remained over an action movie");
    safe = true;
    native_input = 0;
    ++targets[0].identity;
    reveal::update(nullptr, true);
    shown(true, "Controller hold was lost after a transition movie");
    safe = false;
    native_input = game::edition().movie;
    reveal::update(nullptr, true);
    reveal::controller(false);
    reveal::update(nullptr, true);
    safe = true;
    native_input = 0;
    reveal::update(nullptr, true);
    shown(false, "Controller released during movie revealed the next scene");
    reveal::controller(false);
    reveal::update(nullptr, true);
    reveal::controller(true);
    reveal::update(nullptr, true);
    shown(true, "Controller release did not rearm reveal");
    reveal::suspend();
    reveal::controller(true);
    reveal::update(nullptr, true);
    shown(false, "Suspended controller accepted the held trigger");
    reveal::controller(false);
    reveal::update(nullptr, true);
    reveal::controller(true);
    reveal::update(nullptr, true);
    shown(true, "Fresh controller hold failed after suspension");
    options.hotspot_reveal_key = 'R';
    press('R');
    reveal::controller(false);
    reveal::update(nullptr, true);
    shown(true, "Controller release cancelled independent keyboard hold");
    release('R');
    reveal::controller(true);
    reveal::update(nullptr, false);
    shown(false, "Disabled input context accepted controller reveal");
    reveal::update(nullptr, true);
    shown(false, "Controller hold rearmed across disabled input context");
}
}

const Settings& settings() {
    return options;
}

namespace enhancements {
bool game_is_foreground(HWND) {
    return foreground;
}

bool inventory_focused(HWND) {
    return inventory;
}

bool scene_overlay_active() {
    return paused;
}

bool safe_save_available() {
    return safe;
}

const Dialogue* current_dialogue() {
    return dialogue;
}

namespace quick_menu {
bool expanded() {
    return quick;
}
}

namespace game {
MainView* current_view() {
    return &views[view_index];
}

const Edition& edition() {
    static const Edition profile = [] {
        Edition result{};
        result.movie = 1;
        result.action_movie = 2;
        result.main_menu = 3;
        return result;
    }();
    return profile;
}

std::uintptr_t input_vtable() {
    return native_input;
}

ScriptControls script_controls() {
    return script;
}

std::vector<RECT> emotion_targets() {
    return emotions;
}

RECT scene_bounds() {
    return {0, 50, 640, 430};
}

std::vector<WorldTarget> world_targets(bool) {
    return targets;
}
}
}

namespace native_game {
void set_canvas_reveal(CanvasSource callback) {
    reveal_canvas = callback;
}

void invalidate_canvas() {
    ++redraws;
}
}

int main() {
    try {
        keyboard();
        transitions();
        controller_hold();
        reset();
        options.hotspot_reveal_key = 'H';
        press('H');
        const auto before = redraws;
        targets[0].interaction = enhancements::game::Interaction::item;
        enhancements::reveal::update(nullptr, true);
        require(redraws > before, "Stationary target's changed action kind was not redrawn");
        for (const auto kind :
             {enhancements::game::Interaction::view, enhancements::game::Interaction::talk,
              enhancements::game::Interaction::click}) {
            const auto previous = redraws;
            targets[0].interaction = kind;
            enhancements::reveal::update(nullptr, true);
            require(redraws > previous, "A stationary cursor label change was not redrawn");
        }
        for (const auto labels : {false, true}) {
            const auto previous = redraws;
            options.hotspot_labels = labels;
            enhancements::reveal::update(nullptr, true);
            shown(true, "Changing action labels hid the target symbols");
            require(redraws > previous, "A stationary label option change was not redrawn");
        }
        enhancements::reveal::release();
        shown(false, "Reveal canvas survived release");
        std::cout << "Hotspot reveal input and context transitions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
