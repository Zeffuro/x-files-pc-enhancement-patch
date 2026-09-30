#include "enhancements/screens.h"
#include "enhancements/focus.h"
#include "enhancements/dialogue.h"
#include "enhancements/game_ui.h"
#include "enhancements/menu_corner.h"
#include "enhancements/ui/quick_menu.h"
#include "enhancements/ui/quick_menu_surface.h"
#include "settings.h"

#include <iostream>
#include <stdexcept>

namespace {
namespace menu = enhancements::quick_menu;
constexpr RECT native_button{501, 0, 640, 34};
constexpr RECT world_target{100, 100, 120, 120};
Settings options;
bool browser = false, transcript_open = false, other_script_button = false;
unsigned save_requests = 0, load_requests = 0;

struct PointCall {
    RECT target;
    bool activate;
    bool right;
};

std::vector<PointCall> calls;

void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

void align_window(HWND window, RECT target) {
    POINT cursor{};
    require(GetCursorPos(&cursor) != FALSE, "Cannot read the existing cursor");
    require(SetWindowPos(window, nullptr, cursor.x - (target.left + target.right) / 2,
                         cursor.y - (target.top + target.bottom) / 2, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
            "Cannot align the hidden navigation window");
}

class Fixture {
public:
    HWND window = nullptr;

    Fixture() {
        POINT cursor{};
        require(GetCursorPos(&cursor) != FALSE, "Cannot read cursor for the hidden window");
        window = CreateWindowExW(0, L"STATIC", L"Gameplay menu navigation test", WS_POPUP,
                                 cursor.x - 110, cursor.y - 110, 640, 480, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Cannot create the hidden navigation window");
        options.save_browser = options.dialogue_transcript = true;
    }

    ~Fixture() {
        menu::release();
        DestroyWindow(window);
    }

    void reset(bool analog) {
        menu::release();
        browser = transcript_open = other_script_button = false;
        options.analog_cursor = analog;
        save_requests = load_requests = 0;
        calls.clear();
        menu::update();
        align_window(window, world_target);
    }

    bool navigate(int horizontal = 0, int vertical = 0, bool activate = false, bool cancel = false,
                  bool inventory = false, bool keyboard = false) {
        return enhancements::navigate_screen(window, horizontal, vertical, activate, cancel,
                                             inventory, keyboard);
    }

    void expand() {
        align_window(window, menu::Surface::toggle(native_button));
        require(navigate(0, 0, true) && menu::expanded(),
                "Controller activation did not click the actual collapsed menu toggle");
        for (unsigned i = 0; i < 8; ++i) {
            Sleep(16);
            menu::update();
        }
    }
};
}

const Settings& settings() {
    return options;
}

namespace enhancements {
bool refresh_native_cursor(POINT) {
    return true;
}

bool checkpoint_available() {
    return true;
}

bool scene_overlay_available(bool) {
    return true;
}

bool export_save_available() {
    return true;
}

bool game_is_foreground(HWND) {
    return false;
}

const Dialogue* current_dialogue() {
    return nullptr;
}

bool inventory_focused(HWND) {
    return false;
}

bool settings_link_visible() {
    return false;
}

void request_settings(HWND) {
    throw std::runtime_error("Menu navigation requested unrelated settings");
}

void open_text_entry(HWND, const RECT&, unsigned) {
    throw std::runtime_error("Menu navigation requested unrelated text entry");
}

std::vector<RECT> main_menu_targets(bool) {
    return {};
}

int directional_target(std::span<const RECT>, POINT, int, int) {
    return -1;
}

int hotspot_target(std::span<const RECT>, POINT, int) {
    return -1;
}

bool point_controller(HWND window, const RECT& target, bool activate, bool right) {
    calls.push_back({target, activate, right});
    const auto targets = menu::targets();
    const bool menu_target = std::any_of(targets.begin(), targets.end(), [&](const RECT& item) {
        return EqualRect(&target, &item);
    });
    if (menu_target && activate) {
        align_window(window, target);
        require(menu::message(window, WM_LBUTTONDOWN, 0, 0) &&
                    menu::message(window, WM_LBUTTONUP, 0, 0),
                "Recorded controller click did not reach the actual toolbar input handler");
    }
    return true;
}
}

namespace enhancements::game {
const Edition& edition() {
    return dvd;
}

std::uintptr_t input_vtable() {
    return 0;
}

bool saving_available() {
    return true;
}

std::vector<RECT> modal_buttons() {
    return {};
}

std::vector<RECT> emotion_targets() {
    return {};
}

ScriptControls script_controls() {
    ScriptControls controls;
    if (other_script_button) {
        controls.buttons.push_back(world_target);
    }
    return controls;
}

std::optional<RECT> menu_corner() {
    return native_button;
}

bool suppress_menu_corner(bool) {
    return true;
}

std::optional<MenuCornerIdentity> menu_corner_identity() {
    return MenuCornerIdentity{};
}
}

namespace native_game {
bool native_render_available() {
    return true;
}

void invalidate_canvas() {}
}

namespace saves {
bool browser_active() {
    return browser;
}

bool show_browser(bool saving) {
    saving ? ++save_requests : ++load_requests;
    browser = true;
    return true;
}
}

namespace transcript {
bool active() {
    return transcript_open;
}

bool show() {
    transcript_open = true;
    return true;
}
}

int main() {
    try {
        Fixture fixture;
        for (const bool analog : {false, true}) {
            fixture.reset(analog);
            require(enhancements::game::script_controls().buttons.empty(),
                    "Native corner was incorrectly represented as a script button");
            for (const bool keyboard : {false, true}) {
                require(!fixture.navigate(0, 0, true, false, false, keyboard),
                        "Menu-only script intercepted Activate away from the toolbar");
                for (const POINT direction :
                     {POINT{-1, 0}, POINT{1, 0}, POINT{0, -1}, POINT{0, 1}}) {
                    require(
                        !fixture.navigate(direction.x, direction.y, false, false, false, keyboard),
                        "Menu-only script intercepted ordinary world directional input");
                }
                require(!fixture.navigate(0, 0, false, true, false, keyboard) &&
                            !fixture.navigate(0, 0, false, false, true, keyboard),
                        "Menu-only script intercepted world Back or Inventory input");
            }
            require(calls.empty() && !menu::expanded(),
                    "Ordinary exploration input generated a menu click");

            fixture.expand();
            const auto toggle = menu::Surface::toggle(native_button);
            require(calls.size() == 1 && EqualRect(&calls.back().target, &toggle) &&
                        calls.back().activate && !calls.back().right,
                    "Collapsed navigation clicked the old native rectangle instead of its toggle");

            calls.clear();
            const auto load = menu::Surface::button(native_button, 1);
            align_window(fixture.window, load);
            require(fixture.navigate(0, 0, true) && calls.size() == 1 &&
                        EqualRect(&calls.back().target, &load) && calls.back().activate &&
                        load_requests == 1 && save_requests == 0 && !menu::expanded(),
                    "Expanded Activate advanced away from the current action or missed its click");

            fixture.reset(analog);
            fixture.expand();
            calls.clear();
            require(fixture.navigate(0, 0, false, true) && !menu::expanded() && calls.empty(),
                    "Expanded Back did not dismiss the toolbar without clicking a scene target");

            other_script_button = true;
            align_window(fixture.window, world_target);
            require(fixture.navigate(0, 0, true) && calls.size() == 1 &&
                        EqualRect(&calls.back().target, &world_target),
                    "Ignoring the native menu also bypassed a real script screen control");
        }
        fixture.reset(true);
        options.quick_menu_items = {false, true, false, true, false};
        menu::update();
        fixture.expand();
        const auto compact = menu::targets();
        require(compact.size() == 3, "Navigation retained hidden menu items");
        calls.clear();
        align_window(fixture.window, compact.front());
        require(fixture.navigate(0, 0, true) && calls.size() == 1 &&
                    EqualRect(&calls.back().target, &compact.front()) && load_requests == 1 &&
                    save_requests == 0,
                "Controller activation lost Load identity after hidden items were packed");
        require(!IsWindowVisible(fixture.window), "Navigation test exposed a visible window");
        std::cout << "Menu-only exploration and collapsed/expanded controller navigation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
