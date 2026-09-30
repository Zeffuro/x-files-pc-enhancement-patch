#include "transcript/view.h"
#include "transcript/capture.h"
#include "enhancements/game_ui.h"
#include "game/render/native_render.h"
#include "playback/fast_forward.h"
#include "enhancements/scene_overlay.h"
#include "settings.h"

#include <iostream>
#include <stdexcept>

namespace {
bool menu = true, confirmation = false, browser = false;
unsigned suspensions = 0;
native_game::CanvasSource overlay = nullptr;
transcript::History recorded;
Settings options;

void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

class Window {
public:
    HWND handle = nullptr;

    Window() {
        POINT cursor{};
        require(GetCursorPos(&cursor) != FALSE, "Cannot obtain cursor position");
        handle = CreateWindowExW(0, L"STATIC", L"Transcript input test", WS_POPUP, cursor.x - 364,
                                 cursor.y - 453, 640, 480, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        require(handle != nullptr, "Cannot create hidden test window");
    }

    ~Window() {
        transcript::release();
        DestroyWindow(handle);
    }
};
}

namespace enhancements {
bool checkpoint_available() {
    return false;
}

SceneOverlay::~SceneOverlay() {}

bool scene_overlay_available(bool) {
    return false;
}

bool SceneOverlay::begin(bool) {
    return false;
}

bool SceneOverlay::active() const {
    return false;
}

bool SceneOverlay::valid() const {
    return false;
}

void SceneOverlay::end(bool) {}

void suspend_controller() {
    ++suspensions;
}
}

namespace enhancements::quick_menu {
void dismiss() {}

HDC canvas(HDC dc) {
    return dc;
}
}

const Settings& settings() {
    return options;
}

namespace enhancements::game {
const Edition& edition() {
    return dvd;
}

std::uintptr_t input_vtable() {
    return menu ? edition().main_menu : 0;
}

bool menu_confirmation_active() {
    return confirmation;
}
}

namespace saves {
bool browser_active() {
    return browser;
}
}

namespace native_game {
bool native_render_available() {
    return true;
}

void set_canvas_overlay(CanvasSource callback) {
    overlay = callback;
}

HDC canvas_dc() {
    return nullptr;
}

void invalidate_canvas() {}
}

namespace transcript {
const History& history() {
    return recorded;
}
}

int main() {
    try {
        Window window;
        transcript::update();
        require(transcript::available() && overlay, "Transcript did not attach its canvas");
        const auto send = [&](UINT id, WPARAM key = 0, LPARAM data = 0) {
            return transcript::message(window.handle, id, key, data);
        };
        menu = false;
        require(!send(WM_KEYDOWN, VK_F8) && !transcript::active(),
                "Transcript opened outside the main menu");
        menu = true;
        confirmation = true;
        require(!send(WM_KEYDOWN, VK_F8), "Transcript interrupted a menu confirmation");
        confirmation = false;
        browser = true;
        require(!send(WM_KEYDOWN, VK_F8), "Transcript interrupted the save browser");
        browser = false;

        const auto epoch = playback::movie_speed_input.context_epoch;
        require(send(WM_KEYDOWN, VK_F8) && transcript::active(), "F8 did not open transcript");
        require(suspensions == 1 && playback::movie_speed_input.context_epoch > epoch,
                "Opening transcript did not suspend held input");
        require(send(WM_KEYDOWN, VK_F8, LPARAM{1} << 30) && transcript::active(),
                "Held F8 closed the newly opened transcript");
        require(send(WM_KEYUP, VK_F8), "Transcript leaked the opening key release");
        require(send(WM_KEYDOWN, VK_ESCAPE) && !transcript::active(),
                "Escape did not close transcript");

        require(send(WM_KEYDOWN, VK_F8) && transcript::active(), "Cannot reopen transcript");
        require(send(WM_LBUTTONDOWN), "Transcript leaked a left press");
        require(send(WM_KEYDOWN, VK_ESCAPE) && !transcript::active(),
                "Escape did not close while the mouse was held");
        require(send(WM_LBUTTONUP), "Transcript leaked the held mouse release after closing");

        require(send(WM_KEYDOWN, VK_F8) && transcript::active(), "Cannot reopen for double click");
        require(send(WM_LBUTTONDBLCLK), "Transcript leaked a double-click press");
        send(WM_KEYDOWN, VK_ESCAPE);
        require(send(WM_LBUTTONUP), "Transcript leaked the double-click release after closing");

        require(!send(WM_LBUTTONDOWN), "Removed main-menu link still consumed a press");
        send(WM_KILLFOCUS);
        send(WM_LBUTTONUP);
        require(!transcript::active(), "Stale mouse press opened transcript after focus loss");

        require(send(WM_KEYDOWN, VK_F8) && transcript::active(), "Cannot reopen after focus loss");
        menu = false;
        transcript::update();
        require(!transcript::active(), "Transcript remained open after leaving the menu");
        options.dialogue_transcript = false;
        menu = true;
        require(!transcript::available() && !send(WM_KEYDOWN, VK_F8),
                "Disabled transcript still opened");
        transcript::release();
        require(!overlay && !transcript::available(), "Transcript did not release its callback");
        std::cout << "Transcript input passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
