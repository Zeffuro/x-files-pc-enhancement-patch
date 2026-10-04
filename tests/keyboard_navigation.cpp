#include "enhancements/keyboard_navigation.h"
#include "enhancements/dialogue.h"
#include "enhancements/focus.h"
#include "enhancements/game_ui.h"
#include "enhancements/game_resources.h"
#include "enhancements/ui/menu_link.h"
#include "enhancements/ui/quick_menu.h"
#include "settings.h"

#include <iostream>
#include <stdexcept>

namespace {
POINT test_cursor{};
std::array<bool, 256> test_held{};
bool foreground = true, inventory = false, dialogue_visible = false;
enhancements::game::ScriptControls test_script;
enhancements::Dialogue test_dialogue;
std::vector<RECT> test_icons, test_evidence, test_emotions;
Settings test_options;
std::uintptr_t test_input = 0;
int inventory_direction = 0, dialogue_direction = 0, dialogue_tab = 0;
unsigned groups = 0, closes = 0;

struct Click {
    RECT bounds;
    bool activate;
};

std::vector<Click> calls;

BOOL WINAPI cursor_position(POINT* point) {
    *point = test_cursor;
    return TRUE;
}

BOOL WINAPI client_position(HWND, POINT*) {
    return TRUE;
}

SHORT WINAPI key_state(int key) {
    return test_held.at(key) ? SHORT(-32768) : SHORT(0);
}

void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}
}

#define GetCursorPos cursor_position
#define ScreenToClient client_position
#define GetKeyState key_state
#include "enhancements/screens.cpp"
#include "enhancements/keyboard_navigation.cpp"
#undef GetCursorPos
#undef ScreenToClient
#undef GetKeyState

const Settings& settings() {
    return test_options;
}

namespace enhancements {
bool game_is_foreground(HWND) {
    return foreground;
}

const Dialogue* current_dialogue() {
    return dialogue_visible ? &test_dialogue : nullptr;
}

std::vector<RECT> conversation_evidence() {
    return test_evidence;
}

bool focus_conversation_evidence(HWND) {
    if (!dialogue_visible) {
        return false;
    }
    ++groups;
    return true;
}

bool close_dialogue(HWND) {
    closes += dialogue_visible;
    return dialogue_visible;
}

bool navigate_dialogue(HWND, int direction, int tab) {
    dialogue_direction = direction;
    dialogue_tab = tab;
    return dialogue_visible;
}

bool inventory_focused(HWND) {
    return inventory;
}

std::vector<RECT> inventory_bounds(const game::MainView*) {
    return test_icons;
}

bool focus_inventory(HWND) {
    inventory = !inventory;
    ++groups;
    return true;
}

bool leave_inventory(HWND) {
    const bool result = inventory;
    inventory = false;
    return result;
}

bool navigate_inventory(HWND, int direction) {
    inventory_direction = direction;
    return inventory;
}

bool point_controller(HWND, const RECT& target, bool activate, bool) {
    test_cursor = {(target.left + target.right) / 2, (target.top + target.bottom) / 2};
    calls.push_back({target, activate});
    return true;
}

int directional_target(std::span<const RECT> targets, POINT point, int horizontal, int vertical) {
    if (targets.empty() || (!horizontal && !vertical)) {
        return -1;
    }
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (PtInRect(&targets[i], point)) {
            return static_cast<int>((i + targets.size() + (horizontal ? horizontal : vertical)) %
                                    targets.size());
        }
    }
    return horizontal < 0 || vertical < 0 ? static_cast<int>(targets.size()) - 1 : 0;
}

int hotspot_target(std::span<const RECT> targets, POINT point, int direction) {
    return directional_target(targets, point, direction, 0);
}

std::vector<RECT> main_menu_targets(bool) {
    return {{472, 75, 639, 120}, {472, 125, 639, 170}};
}

bool settings_link_visible() {
    return false;
}

void request_settings(HWND) {
    throw std::runtime_error("Unexpected settings request");
}

void open_text_entry(HWND, const RECT&, unsigned) {
    throw std::runtime_error("Keyboard navigation opened the controller text editor");
}
}

namespace enhancements::game {
const Edition& edition() {
    return dvd;
}

std::uintptr_t input_vtable() {
    return test_input;
}

bool saving_available() {
    return true;
}

std::vector<RECT> modal_buttons() {
    return {};
}

std::vector<RECT> emotion_targets() {
    return test_emotions;
}

MainView* current_view() {
    return nullptr;
}

ScriptControls script_controls() {
    return test_script;
}
}

namespace enhancements::quick_menu {
bool expanded() {
    return false;
}

std::vector<RECT> targets() {
    return {};
}

void dismiss() {}
}

namespace {
using namespace enhancements;

bool key(WPARAM value) {
    return navigate_keyboard(nullptr, value);
}

void reset() {
    foreground = true;
    inventory = dialogue_visible = false;
    test_input = 0;
    test_script = {};
    test_dialogue = {};
    test_icons.clear();
    test_evidence.clear();
    test_emotions.clear();
    calls.clear();
    test_held.fill(false);
    test_cursor = {500, 350};
    groups = closes = 0;
    inventory_direction = dialogue_direction = dialogue_tab = 0;
}

void selected(const RECT& bounds, bool activate, const char* reason) {
    require(calls.size() == 1 && EqualRect(&calls.front().bounds, &bounds) &&
                calls.front().activate == activate,
            reason);
    calls.clear();
}

void conversation_inventory() {
    reset();
    dialogue_visible = true;
    test_dialogue.count = 2;
    test_dialogue.choices[0] = {200, 100, 350, 130};
    test_dialogue.choices[1] = {200, 140, 350, 170};
    test_dialogue.talk = {200, 50, 250, 80};
    test_dialogue.history = {260, 50, 330, 80};
    test_evidence = {{20, 100, 50, 130}, {70, 100, 100, 130}};
    require(key(VK_DOWN) && dialogue_direction == 1 && dialogue_tab == 0,
            "Down did not route to native response navigation");
    require(key(VK_LEFT) && dialogue_direction == 0 && dialogue_tab == -1,
            "Left did not route to native Talk tab navigation");
    require(key(VK_TAB) && groups == 1, "Tab did not switch conversation groups");
    require(key(VK_RETURN), "Enter was not consumed in a conversation");
    selected(test_dialogue.choices[0], false, "Enter chose an unfocused response");
    require(key(VK_RETURN), "Enter did not activate a focused response");
    selected(test_dialogue.choices[0], true, "Enter missed the response's native click target");
    test_cursor = {80, 110};
    require(key(VK_RETURN), "Enter did not activate evidence");
    selected(test_evidence[1], true, "Enter lost evidence action identity");
    test_cursor = {280, 65};
    key(VK_RETURN);
    selected(test_dialogue.history, true, "Enter did not click a selected native dialogue tab");
    require(key(VK_BACK) && closes == 1, "Back did not close the conversation");

    reset();
    test_icons = {{10, 420, 40, 470}, {80, 420, 120, 470}};
    require(key(VK_TAB) && inventory && key(VK_UP) && inventory_direction == -1,
            "Tab/Up did not focus and navigate inventory");
    require(key(VK_DOWN) && inventory_direction == 1 && key(VK_LEFT) && inventory_direction == -1 &&
                key(VK_RIGHT) && inventory_direction == 1,
            "Inventory arrows did not share a consistent direction");
    test_cursor = {95, 440};
    require(key(VK_RETURN), "Enter did not consume inventory activation");
    selected(test_icons[1], true, "Inventory activation lost the selected native icon");
    require(key(VK_TAB) && !inventory, "Tab did not return from inventory");

    reset();
    test_emotions = {{30, 120, 60, 160}, {80, 120, 110, 160}};
    require(key(VK_TAB), "Tab did not focus native emotion controls");
    selected(test_emotions.front(), false, "Emotion focus missed its first native target");
    require(key(VK_RIGHT), "Right did not navigate native emotion controls");
    selected(test_emotions.back(), false, "Emotion selection lost its next native target");
    require(key(VK_RETURN), "Enter did not activate a selected native emotion");
    selected(test_emotions.back(), true, "Enter missed the selected native emotion");
}

void devices_typing() {
    reset();
    test_script.resources = {resource::pda_message};
    test_script.buttons = {{422, 54, 435, 72}, {422, 342, 435, 361}, {300, 360, 335, 382}};
    test_cursor = {300, 180};
    require(key(VK_DOWN), "PDA Down did not reach native scrolling");
    selected(test_script.buttons[1], true, "PDA Down missed its native arrow");
    require(key(VK_TAB) && test_cursor.x == 350 && test_cursor.y == 399,
            "PDA Tab did not focus the active native toolbar tab");
    require(key(VK_TAB) && test_cursor.x == 300 && test_cursor.y == 200,
            "PDA Tab did not return to document content");

    reset();
    test_script.resources = {resource::workstation_root, resource::workstation_message};
    test_script.buttons = {{24, 98, 113, 121}, {601, 99, 615, 116}, {601, 435, 615, 450}};
    test_cursor = {300, 180};
    require(key(VK_UP), "Workstation Up did not reach native scrolling");
    selected(test_script.buttons[1], true, "Workstation Up missed its native arrow");
    require(key(VK_TAB) && test_cursor.x < 115, "Workstation Tab did not focus its sidebar");
    require(key(VK_TAB) && test_cursor.x > 115, "Workstation Tab did not return to content");

    for (const auto resource :
         {resource::workstation_login, resource::workstation_search, resource::workstation_media,
          resource::save, resource::pda_notes, 98765u}) {
        reset();
        test_script.resources = {resource};
        if (resource == resource::workstation_login || resource == resource::workstation_search ||
            resource == resource::workstation_media) {
            test_script.resources.push_back(resource::workstation_root);
        }
        test_script.text_input = true;
        test_script.buttons = {{330, 249, 610, 269}};
        for (const auto value :
             {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_RETURN, VK_BACK, VK_TAB, int('A')}) {
            require(!key(value), "Native editor lost ordinary typing or navigation keys");
        }
        require(calls.empty() && !inventory && groups == 0,
                "Native editor generated a click or changed focus groups");
    }
    reset();
    test_script.script_dialog = true;
    test_script.dialog_buttons = {{200, 200, 300, 250}};
    test_script.dialog_fields = {{100, 100, 300, 125}};
    test_script.text_input = true;
    require(!key(VK_RETURN) && !key(VK_TAB) && !key(VK_BACK) && !key(VK_LEFT) && calls.empty(),
            "A native script dialog field lost editing input");

    reset();
    test_script.resources = {resource::workstation_root};
    require(!key(VK_RETURN) && !key(VK_DOWN) && calls.empty(),
            "A closed workstation's retained root generated navigation");
}

void ownership_guards() {
    reset();
    inventory = true;
    test_icons = {{10, 420, 40, 470}};
    for (const auto modifier : {VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN}) {
        test_held[modifier] = true;
        require(!key(VK_RETURN) && !key(VK_TAB) && !key(VK_LEFT),
                "A modified/system shortcut was consumed");
        test_held[modifier] = false;
    }
    foreground = false;
    require(!key(VK_RETURN) && !key(VK_TAB) && calls.empty(),
            "Unfocused input activated a native target");
    NavigationKeys keys;
    for (const auto value : {VK_RETURN, VK_TAB, VK_BACK, VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN}) {
        require(!keys.owns(WM_KEYUP, value, 0), "An unowned release was consumed");
        keys.consume(value);
        require(keys.owns(WM_KEYDOWN, value, LPARAM{1} << 30),
                "A held navigation key repeated activation");
        const bool character = value == VK_RETURN || value == VK_TAB || value == VK_BACK;
        require(keys.owns(WM_CHAR, value, 0) == character,
                "Character ownership did not distinguish text from virtual arrow codes");
        require(!keys.owns(WM_CHAR, 'Q', 0), "Unrelated native typing was consumed");
        require(keys.owns(WM_KEYUP, value, 0) && !keys.owns(WM_CHAR, value, 0),
                "Consumed navigation release retained stale ownership");
        keys.consume(value);
        keys.reset();
        require(!keys.owns(WM_KEYDOWN, value, LPARAM{1} << 30),
                "Focus loss retained held navigation ownership");
        keys.consume(value);
        require(keys.owns(WM_SYSKEYDOWN, value, LPARAM{1} << 30) &&
                    keys.owns(WM_SYSCHAR, value, 0) == character &&
                    keys.owns(WM_SYSKEYUP, value, 0) && !keys.owns(WM_KEYUP, value, 0) &&
                    !keys.owns(WM_SYSCHAR, value, 0),
                "Alt acquired during a consumed press lost system-key release ownership");
        keys.consume(value);
        foreground = false;
        require(keys.owns(WM_KEYDOWN, value, LPARAM{1} << 30),
                "A consumed hold escaped ownership after focus loss");
        foreground = true;
        require(keys.owns(WM_KEYDOWN, value, LPARAM{1} << 30),
                "Returning focus reactivated a consumed hold");
        require(!keys.owns(WM_KEYDOWN, value, 0) && !keys.owns(WM_CHAR, value, 0),
                "A fresh press in a native editor retained stale navigation ownership");
    }
    for (const auto arrow : {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN}) {
        keys.consume(arrow);
        require(!keys.owns(WM_KEYDOWN, '5', 0) && !keys.owns(WM_CHAR, arrow, 0) &&
                    !keys.owns(WM_SYSCHAR, arrow, 0),
                "A missed arrow release swallowed native punctuation after focus returned");
        require(keys.owns(WM_KEYDOWN, arrow, LPARAM{1} << 30),
                "Unrelated typing rearmed a stale held arrow");
    }
    keys.consume(VK_F11);
    require(!keys.owns(WM_CHAR, 'z', 0), "A held function key swallowed native text");
    keys.consume(300);
    require(!keys.owns(WM_KEYUP, 300, 0), "Out-of-range key gained ownership");
}
}

int main() {
    try {
        conversation_inventory();
        devices_typing();
        ownership_guards();
        std::cout << "Keyboard native activation, groups, typing and key ownership passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
