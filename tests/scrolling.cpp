#include <windows.h>
#include <algorithm>
#include <vector>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
POINT test_cursor{300, 180};
unsigned pointer_moves = 0;
bool left_held = false;
bool right_held = false;

BOOL WINAPI test_get_cursor(LPPOINT point) {
    *point = test_cursor;
    return TRUE;
}

BOOL WINAPI test_set_cursor(int x, int y) {
    ++pointer_moves;
    test_cursor = {x, y};
    return TRUE;
}

BOOL WINAPI test_coordinates(HWND, LPPOINT) {
    return TRUE;
}

SHORT WINAPI test_key_state(int key) {
    return ((key == VK_LBUTTON && left_held) || (key == VK_RBUTTON && right_held)) ? SHORT(-32768)
                                                                                   : SHORT(0);
}
}

#define GetCursorPos test_get_cursor
#define SetCursorPos test_set_cursor
#define ScreenToClient test_coordinates
#define ClientToScreen test_coordinates
#define GetAsyncKeyState test_key_state
#include "enhancements/scrolling.cpp"
#undef GetCursorPos
#undef SetCursorPos
#undef ScreenToClient
#undef ClientToScreen
#undef GetAsyncKeyState

namespace enhancements {

Dialogue* dialogue_fixture = nullptr;
bool foreground_fixture = true;
bool inventory_fixture = false;
bool text_fixture = false;
bool menu_fixture = false;
bool reader_fixture = false;
bool controller_click_fixture = false;
int dialogue_steps = 0;

const Dialogue* current_dialogue() {
    return dialogue_fixture;
}

bool scroll_dialogue(int direction) {
    dialogue_steps += direction;
    return true;
}

bool inventory_focused(HWND) {
    return inventory_fixture;
}

bool game_is_foreground(HWND) {
    return foreground_fixture;
}

bool controller_inventory_click_pending() {
    return controller_click_fixture;
}

bool text_entry_busy() {
    return text_fixture;
}

namespace quick_menu {
bool expanded() {
    return menu_fixture;
}
}

namespace documents {
bool active() {
    return reader_fixture;
}
}

namespace game {
MainView* view_fixture = nullptr;
void* input_fixture = nullptr;
std::uintptr_t vtable_fixture = 0;
bool modal_fixture = false;
bool confirmation_fixture = false;
ScriptControls script_fixture;
std::vector<RECT> activated;
bool activation_fixture = true;
void (*activation_effect)() = nullptr;

bool activate_script_button(unsigned resource, const RECT& bounds) {
    if (resource != script_fixture.resources.back() || !activation_fixture) {
        return false;
    }
    activated.push_back(bounds);
    if (activation_effect) {
        activation_effect();
    }
    return true;
}

MainView* current_view() {
    return view_fixture;
}

void* current_input() {
    return input_fixture;
}

std::uintptr_t input_vtable() {
    return vtable_fixture;
}

bool menu_confirmation_active() {
    return confirmation_fixture;
}

std::vector<RECT> modal_buttons() {
    return modal_fixture ? std::vector<RECT>{{0, 0, 20, 20}} : std::vector<RECT>{};
}

ScriptControls script_controls() {
    return script_fixture;
}
}

}

namespace transcript {
bool active_fixture = false;

bool active() {
    return active_fixture;
}
}

namespace saves {
bool active_fixture = false;

bool browser_active() {
    return active_fixture;
}
}

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void wheel_cases() {
    using enhancements::scrolling::Wheel;
    Wheel wheel;
    require(wheel.add(30) == 0 && wheel.add(30) == 0 && wheel.add(59) == 0 && wheel.add(1) == 1,
            "Partial positive wheel deltas did not form one detent.");
    require(wheel.add(-50) == 0 && wheel.add(-70) == -1,
            "Partial negative wheel deltas did not form one detent.");
    require(wheel.add(90) == 0 && wheel.add(-90) == 0 && wheel.add(120) == 1,
            "Reversing partial wheel motion accumulated extra scrolling.");
    require(wheel.add(-360) == -3 && wheel.add(480) == 4,
            "Multiple wheel detents lost direction or count.");
    require(wheel.add(60) == 0, "Partial reset fixture scrolled early.");
    wheel.reset();
    require(wheel.add(60) == 0, "Reset carried a partial detent into the new context.");
    wheel.reset();
    require(wheel.add(12000) == 8 && wheel.add(0) == 0,
            "Large wheel motion was not bounded or leaked into a later event.");
    require(wheel.add(-12000) == -8 && wheel.add(0) == 0,
            "Large reverse wheel motion was not bounded.");
    wheel.reset();
    require(wheel.add(std::numeric_limits<int>::max()) == 8 && wheel.add(0) == 0,
            "Extreme wheel input overflowed or retained extra whole detents.");
}

void target_cases() {
    using namespace enhancements;

    struct Case {
        unsigned resource;
        RECT up, down;
        POINT content, toolbar;
    };

    const Case cases[]{
        {resource::pda_notes, {416, 87, 429, 105}, {416, 330, 429, 349}, {300, 180}, {281, 399}},
        {resource::pda_message, {420, 55, 435, 72}, {420, 343, 435, 362}, {300, 180}, {350, 399}},
        {resource::workstation_message,
         {600, 99, 615, 116},
         {600, 435, 615, 452},
         {300, 180},
         {80, 110}},
    };
    for (const auto& fixture : cases) {
        game::ScriptControls script;
        script.resources = {resource::workstation_root, fixture.resource};
        script.buttons = {fixture.up, fixture.down, {225, 175, 415, 190}};
        script.text_input = true;
        auto target = scrolling::device_target(script);
        require(target.resource == fixture.resource && EqualRect(&target.up, &fixture.up) &&
                    EqualRect(&target.down, &fixture.down),
                "Known page did not retain its native callback-backed arrow controls.");
        require(PtInRect(&target.content, fixture.content) &&
                    !PtInRect(&target.content, fixture.toolbar) &&
                    !PtInRect(&target.content, POINT{(fixture.up.left + fixture.up.right) / 2,
                                                     (fixture.up.top + fixture.up.bottom) / 2}),
                "Content scrolling included toolbar or arrow controls.");
        script.buttons.pop_back();
        script.buttons.pop_back();
        require(!scrolling::device_target(script).resource,
                "One arrow was enough to accept an incomplete or stale native page.");
        script.buttons = {{0, 0, 640, 480}};
        require(!scrolling::device_target(script).resource,
                "A catch-all callback was treated as a native scrolling arrow.");
        script.buttons = {fixture.up, fixture.down};
        script.script_dialog = true;
        require(!scrolling::device_target(script).resource, "Modal script dialog was scrolled.");
        script.script_dialog = false;
        script.acknowledgement_buttons.push_back({250, 250, 300, 280});
        require(!scrolling::device_target(script).resource, "Acknowledgement was scrolled.");
        script.acknowledgement_buttons.clear();
        for (const auto excluded :
             {resource::save, resource::load, resource::options, resource::help, resource::phone}) {
            script.resources.push_back(excluded);
            require(!scrolling::device_target(script).resource,
                    "Other native page was scrolled through a stale device resource.");
            script.resources.pop_back();
        }
    }
    game::ScriptControls unrelated;
    unrelated.resources = {resource::workstation_root, resource::workstation_search};
    unrelated.buttons = {cases[2].up, cases[2].down};
    require(!scrolling::device_target(unrelated).resource,
            "Workstation typing/search or stale root was treated as a scrollable message.");
    unrelated.resources = {resource::pda_inbox};
    require(!scrolling::device_target(unrelated).resource,
            "An inbox wheel activated or navigated native message responses.");
    unrelated.resources.clear();
    require(!scrolling::device_target(unrelated).resource,
            "Ordinary scene accepted device scrolling.");
}

void context_cases() {
    using namespace enhancements;
    game::MainView first{}, second{};
    game::view_fixture = &first;
    game::script_fixture.resources = {resource::pda_message};
    game::script_fixture.buttons = {{420, 55, 435, 72}, {420, 343, 435, 362}};
    const auto initial = scrolling::context();
    require(initial.resource == resource::pda_message,
            "Known page was absent from the runtime context.");
    for (auto* blocked :
         {&game::modal_fixture, &game::confirmation_fixture, &text_fixture, &menu_fixture,
          &reader_fixture, &transcript::active_fixture, &saves::active_fixture}) {
        *blocked = true;
        require(!scrolling::context().resource, "A modal or patch overlay accepted scrolling.");
        *blocked = false;
    }
    game::vtable_fixture = 123;
    require(!scrolling::context().resource, "Native scene/movie input accepted device scrolling.");
    game::vtable_fixture = 0;
    Dialogue frame;
    frame.viewport = {239, 305, 451, 395};
    dialogue_fixture = &frame;
    const auto talk = scrolling::context();
    require(talk.resource == 1 && EqualRect(&talk.target.content, &frame.viewport),
            "Talk wheel bounds did not use the captured native text viewport.");
    frame.is_history = true;
    require(!scrolling::same(talk, scrolling::context()),
            "History inherited Talk partial wheel state.");
    dialogue_fixture = nullptr;
    game::input_fixture = &second;
    require(!scrolling::same(initial, scrolling::context()),
            "Native input replacement retained pending scroll state.");
    game::input_fixture = nullptr;
    scrolling::previous = initial;
    scrolling::scroll_window = reinterpret_cast<HWND>(1);
    scrolling::queued = 4;
    scrolling::wheel.add(60);
    game::view_fixture = &second;
    scrolling::update(true);
    require(!scrolling::queued && !scrolling::scroll_window && scrolling::wheel.add(60) == 0,
            "Context transition retained pending detents or a partial wheel delta.");
    scrolling::queued = -4;
    scrolling::update(false);
    require(!scrolling::queued && scrolling::wheel.add(60) == 0,
            "Focus loss retained pending scroll state.");
    scrolling::reset();
    foreground_fixture = false;
    require(!scrolling::message(nullptr, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), 0),
            "Unfocused wheel input was consumed or dispatched.");
    foreground_fixture = true;
    game::view_fixture = nullptr;
    game::script_fixture = {};
}

void delivery_cases() {
    using namespace enhancements;
    game::MainView first{}, second{};
    const auto window = reinterpret_cast<HWND>(1);
    game::view_fixture = &first;
    game::script_fixture.resources = {resource::pda_notes};
    game::script_fixture.buttons = {{416, 87, 429, 105}, {416, 330, 429, 349}};
    test_cursor = {300, 180};
    const auto wheel_message = [&](int delta, unsigned keys = 0) {
        return scrolling::message(window, WM_MOUSEWHEEL, MAKEWPARAM(keys, delta), 0);
    };
    const auto count = [] { return game::activated.size(); };
    require(wheel_message(-360) && count() == 3,
            "Multiple detents did not activate exactly three native note arrows.");
    for (const auto& bounds : game::activated) {
        require(EqualRect(&bounds, &game::script_fixture.buttons[1]),
                "Downward notes scrolling activated another control.");
    }
    require(wheel_message(120) && count() == 4 &&
                EqualRect(&game::activated.back(), &game::script_fixture.buttons[0]),
            "Upward notes scrolling did not activate the native up arrow.");
    require(test_cursor.x == 300 && test_cursor.y == 180 && !pointer_moves,
            "Native arrow activation physically moved the pointer.");
    require(wheel_message(-12000) && count() == 12,
            "Large wheel input exceeded the bounded native activation count.");
    for (const bool replace_input : {true, false}) {
        game::activation_effect = replace_input ? +[] { game::input_fixture = game::view_fixture; }
                                                : +[] { ++game::script_fixture.buttons[0].right; };
        const auto before = count();
        require(wheel_message(-360) && count() == before + 1 && !scrolling::queued,
                "Native callback context mutation retained or delivered stale detents.");
        game::input_fixture = nullptr;
        game::script_fixture.buttons = {{416, 87, 429, 105}, {416, 330, 429, 349}};
    }
    game::activation_effect = nullptr;
    game::activation_fixture = false;
    const auto before_failure = count();
    require(wheel_message(-360) && count() == before_failure && !scrolling::queued,
            "Failed native activation retained queued actions.");
    game::activation_fixture = true;
    controller_click_fixture = true;
    require(wheel_message(-360) && count() == before_failure && !scrolling::queued,
            "Pending controller click accepted native wheel activation.");
    controller_click_fixture = false;
    for (auto* held : {&left_held, &right_held}) {
        *held = true;
        require(wheel_message(120) && count() == before_failure,
                "Physically held mouse button accepted native wheel activation.");
        *held = false;
    }
    game::activation_effect = +[] {
        test_cursor = {310, 210};
        scrolling::message(reinterpret_cast<HWND>(1), WM_MOUSEWHEEL, MAKEWPARAM(0, -120), 0);
    };
    require(wheel_message(-360) && count() == before_failure + 1 && test_cursor.x == 310 &&
                test_cursor.y == 210 && !pointer_moves,
            "Reentrant wheel callback overwrote pointer movement or delivered extra actions.");
    game::activation_effect = nullptr;
    require(wheel_message(60) && count() == before_failure + 1,
            "Partial wheel delta activated early.");
    test_cursor = {350, 399};
    scrolling::update(true);
    test_cursor = {300, 180};
    require(wheel_message(60) && count() == before_failure + 1,
            "Leaving content retained partial wheel state.");
    game::view_fixture = &second;
    scrolling::update(true);
    require(wheel_message(60) && count() == before_failure + 1,
            "View replacement retained partial wheel state.");
    scrolling::update(false);
    require(wheel_message(60) && count() == before_failure + 1,
            "Focus loss retained partial wheel state.");
    scrolling::message(window, WM_LBUTTONDOWN, MK_LBUTTON, 0);
    require(wheel_message(60) && count() == before_failure + 1,
            "Native click retained partial wheel state.");
    scrolling::reset();
    for (const unsigned keys : {MK_CONTROL, MK_SHIFT, MK_LBUTTON, MK_RBUTTON}) {
        require(!wheel_message(120, keys), "Modified or held-button wheel was consumed.");
    }
    inventory_fixture = true;
    require(!wheel_message(120), "Inventory focus accepted wheel device scrolling.");
    inventory_fixture = false;
    for (const auto page : {resource::pda_message, resource::workstation_message}) {
        game::script_fixture.resources = {page};
        game::script_fixture.buttons =
            page == resource::pda_message
                ? std::vector<RECT>{{420, 55, 435, 72}, {420, 343, 435, 362}}
                : std::vector<RECT>{{600, 99, 615, 116}, {600, 435, 615, 452}};
        const auto before = count();
        require(wheel_message(-240) && wheel_message(120) && count() == before + 3 &&
                    !pointer_moves,
                "Message or workstation scrolling moved the pointer or lost detents.");
    }
    for (auto effect : {+[] { foreground_fixture = false; }, +[] { test_cursor = {10, 10}; },
                        +[] { game::view_fixture = nullptr; },
                        +[] { game::script_fixture.resources = {resource::pda_inbox}; }}) {
        const auto before = count();
        game::activation_effect = effect;
        require(wheel_message(-360) && count() == before + 1 && !scrolling::queued,
                "Callback focus, pointer, view or page change delivered stale detents.");
        game::activation_effect = nullptr;
        foreground_fixture = true;
        test_cursor = {300, 180};
        game::view_fixture = &first;
        game::script_fixture.resources = {resource::workstation_message};
    }
    Dialogue frame;
    frame.viewport = {239, 305, 451, 395};
    dialogue_fixture = &frame;
    test_cursor = {300, 350};
    dialogue_steps = 0;
    const auto before_talk = count();
    require(wheel_message(-360) && dialogue_steps == 3 && count() == before_talk &&
                test_cursor.x == 300 && test_cursor.y == 350 && !pointer_moves,
            "Talk wheel activated a device control or moved the pointer.");
    frame.is_history = true;
    require(wheel_message(120) && dialogue_steps == 2 && count() == before_talk,
            "History wheel did not use the native up callback.");
    scrolling::reset();
    dialogue_fixture = nullptr;
    game::view_fixture = nullptr;
    game::script_fixture = {};
    game::activated.clear();
}

}

int main() {
    try {
        wheel_cases();
        target_cases();
        context_cases();
        delivery_cases();
        std::cout
            << "Wheel accumulation, bounded detents, native targets and context exclusions pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
