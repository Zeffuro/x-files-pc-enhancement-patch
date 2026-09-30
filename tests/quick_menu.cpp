#include "enhancements/ui/quick_menu.h"
#include "enhancements/ui/quick_menu_surface.h"
#include "enhancements/game_ui.h"
#include "enhancements/menu_corner.h"
#include "enhancements/controls.h"
#include "enhancements/dialogue.h"
#include "game/render/native_render.h"
#include "settings.h"

#include <iostream>
#include <stdexcept>

namespace {
namespace menu = enhancements::quick_menu;
constexpr RECT native_button{501, 0, 640, 34};
Settings options;
bool available = true, can_save = true, browser = false, transcript_open = false;
bool main_menu = false;
bool dialogue = false;
unsigned settings_requests = 0;
unsigned invalidations = 0, save_requests = 0, load_requests = 0, transcript_requests = 0;
unsigned foreground_queries = 0;
unsigned hover_refreshes = 0;
bool corner_suppressed = false;
enhancements::game::MenuCornerIdentity identity{};
bool foreground = false, restore_ok = true;
std::optional<RECT> button_bounds = native_button;

void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

class Fixture {
public:
    HWND window = nullptr;
    HDC background = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    POINT original_cursor{};

    Fixture() {
        require(GetCursorPos(&original_cursor) != FALSE, "Cannot read the existing cursor");
        window = CreateWindowExW(0, L"STATIC", L"Gameplay menu test", WS_POPUP,
                                 original_cursor.x - 618, original_cursor.y - 30, 640, 480, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Cannot create the hidden menu input window");
        background = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 640;
        info.bmiHeader.biHeight = -480;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(background, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        require(background && bitmap, "Cannot create the menu background canvas");
        previous = SelectObject(background, bitmap);
        const RECT bounds{0, 0, 640, 480};
        const auto brush = CreateSolidBrush(RGB(40, 50, 60));
        FillRect(background, &bounds, brush);
        DeleteObject(brush);
        options.save_browser = options.dialogue_transcript = true;
        menu::update();
    }

    ~Fixture() {
        menu::release();
        SelectObject(background, previous);
        DeleteObject(bitmap);
        DeleteDC(background);
        DestroyWindow(window);
    }

    void point(RECT target) {
        POINT cursor{};
        require(GetCursorPos(&cursor) != FALSE, "Cannot read the stationary cursor");
        require(SetWindowPos(window, nullptr, cursor.x - (target.left + target.right) / 2,
                             cursor.y - (target.top + target.bottom) / 2, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "Cannot align the hidden window with an input target");
    }

    bool send(UINT id, WPARAM value = 0) {
        return menu::message(window, id, value, 0);
    }

    void click(RECT target) {
        point(target);
        require(send(WM_LBUTTONDOWN) && send(WM_LBUTTONUP), "Menu leaked a paired toolbar click");
    }

    void expand() {
        browser = transcript_open = false;
        available = true;
        menu::update();
        click(menu::Surface::toggle(native_button));
        require(menu::expanded(), "Toggle did not expand the toolbar");
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
bool checkpoint_available() {
    return available && !dialogue;
}

bool scene_overlay_available(bool) {
    return available;
}

const Dialogue* current_dialogue() {
    static const Dialogue frame;
    return dialogue ? &frame : nullptr;
}

void request_settings(HWND) {
    ++settings_requests;
}

bool export_save_available() {
    return can_save;
}

bool game_is_foreground(HWND) {
    ++foreground_queries;
    return foreground;
}

bool refresh_native_cursor(POINT) {
    ++hover_refreshes;
    return true;
}
}

namespace enhancements::game {
const Edition& edition() {
    return dvd;
}

std::uintptr_t input_vtable() {
    return main_menu ? edition().main_menu : 0;
}

std::optional<RECT> menu_corner() {
    return button_bounds;
}

bool suppress_menu_corner(bool suppress) {
    if (!suppress && !restore_ok) {
        return false;
    }
    corner_suppressed = suppress && button_bounds.has_value();
    return true;
}

std::optional<MenuCornerIdentity> menu_corner_identity() {
    return button_bounds ? std::optional<MenuCornerIdentity>(identity) : std::nullopt;
}
}

namespace native_game {
bool native_render_available() {
    return true;
}

void invalidate_canvas() {
    ++invalidations;
}
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
    ++transcript_requests;
    transcript_open = true;
    return true;
}
}

int main() {
    try {
        Fixture fixture;
        require(menu::targets().size() == 1 && !menu::expanded(),
                "Collapsed menu exposed expanded actions");
        require(corner_suppressed && hover_refreshes == 1,
                "Menu did not suppress native bounds and clear the old hover once");
        require(menu::canvas(fixture.background) == fixture.background,
                "Collapsed menu appeared before hovering the corner");
        menu::update();
        require(hover_refreshes == 1, "Menu repeatedly queued native hover cleanup");
        options.quick_menu = false;
        menu::update();
        require(menu::targets().empty() && !corner_suppressed,
                "Disabled Quick Menu retained native corner ownership");
        options.quick_menu = true;
        options.save_browser = options.dialogue_transcript = false;
        menu::update();
        require(menu::targets().size() == 1 && corner_suppressed,
                "Quick Menu incorrectly depended on transcript or save-browser settings");
        options.save_browser = options.dialogue_transcript = true;
        const auto focused_refreshes = hover_refreshes;
        menu::update(false);
        require(menu::targets().empty() && corner_suppressed,
                "Background game restored the native rollover hotspot");
        require(hover_refreshes == focused_refreshes,
                "Background update queued a cursor refresh using the desktop pointer");
        fixture.point(menu::Surface::toggle(native_button));
        require(!fixture.send(WM_MOUSEMOVE) && !fixture.send(WM_LBUTTONDOWN) &&
                    !fixture.send(WM_LBUTTONUP) && !menu::expanded(),
                "Background hover or click reopened the toolbar");
        options.quick_menu = false;
        menu::update(false);
        require(!corner_suppressed, "Disabling Quick Menu in the background retained ownership");
        options.quick_menu = true;
        menu::update();
        fixture.point(native_button);
        require(!fixture.send(WM_MOUSEMOVE) && corner_suppressed,
                "Menu blocked scaled cursor updates or retained native rollover bounds");
        require(menu::canvas(fixture.background) == fixture.background,
                "Old wide native hotspot still revealed the toolbar");
        fixture.point(menu::Surface::reveal(native_button));
        fixture.send(WM_MOUSEMOVE);
        const auto output = menu::canvas(fixture.background);
        require(output != fixture.background && GetPixel(output, 600, 10) == RGB(0, 0, 0) &&
                    GetPixel(output, 0, 479) == RGB(40, 50, 60),
                "Menu drawing did not replace the native button while preserving the scene");
        fixture.point({100, 100, 120, 120});
        require(!fixture.send(WM_MOUSEMOVE), "Menu swallowed scene hover outside its bounds");
        require(menu::canvas(fixture.background) == fixture.background && corner_suppressed,
                "Leaving the corner retained the hamburger or restored native X rollover");

        fixture.expand();
        require(menu::targets().size() == 6, "Expanded menu omitted an enabled action");
        fixture.point({100, 100, 120, 120});
        fixture.send(WM_MOUSEMOVE);
        require(menu::expanded() && menu::canvas(fixture.background) != fixture.background,
                "Expanded toolbar disappeared when the pointer left the corner");
        fixture.point(menu::Surface::button(native_button, 0));
        const auto before_hover = invalidations;
        require(!fixture.send(WM_MOUSEMOVE) && invalidations > before_hover,
                "Toolbar hover was not redrawn while preserving scaled cursor updates");
        const auto hovered = menu::canvas(fixture.background);
        const auto save_button = menu::Surface::button(native_button, 0);
        bool tooltip_painted = false;
        for (LONG y = save_button.bottom + 2; y < save_button.bottom + 24; ++y) {
            for (LONG x = save_button.left - 35; x < save_button.right + 35; ++x) {
                tooltip_painted = tooltip_painted || GetPixel(hovered, x, y) == RGB(155, 216, 239);
            }
        }
        require(tooltip_painted, "Hovered action did not paint its tooltip on the game canvas");
        const auto load_button = menu::Surface::button(native_button, 1);
        const auto highlighted = [&](HDC dc) {
            unsigned count = 0;
            for (LONG y = load_button.top; y < load_button.bottom; ++y) {
                for (LONG x = load_button.left; x < load_button.right; ++x) {
                    count += GetPixel(dc, x, y) == RGB(155, 216, 239);
                }
            }
            return count;
        };
        require(highlighted(hovered) == 0, "Unhovered Load used the highlight color");
        fixture.point(load_button);
        menu::update();
        require(highlighted(menu::canvas(fixture.background)) > 0,
                "Timer did not refresh hover after the scaled cursor caught up");
        const auto transcript_button = menu::Surface::button(native_button, 2);
        fixture.point(transcript_button);
        fixture.send(WM_MOUSEMOVE);
        const auto transcript_canvas = menu::canvas(fixture.background);
        unsigned transcript_pixels = 0;
        for (LONG y = transcript_button.bottom + 2; y < transcript_button.bottom + 22; ++y) {
            for (LONG x = transcript_button.left - 35; x < transcript_button.right + 35; ++x) {
                transcript_pixels += GetPixel(transcript_canvas, x, y) == RGB(155, 216, 239);
            }
        }
        require(transcript_pixels > 0, "Transcript tooltip text was missing");
        fixture.click(menu::Surface::button(native_button, 0));
        require(save_requests == 1 && browser && !menu::expanded(),
                "Save action did not open the direct save browser");

        fixture.expand();
        fixture.click(menu::Surface::button(native_button, 1));
        require(load_requests == 1 && browser && !menu::expanded(),
                "Load action did not open the direct load browser");

        fixture.expand();
        fixture.click(menu::Surface::button(native_button, 2));
        require(transcript_requests == 1 && transcript_open && !menu::expanded(),
                "Transcript action did not open its view");

        options.dialogue_transcript = false;
        fixture.expand();
        require(menu::targets().size() == 5, "Disabled transcript remained a navigation target");
        fixture.click(menu::Surface::button(native_button, 2));
        require(transcript_requests == 1 && menu::expanded(),
                "Disabled transcript action opened its view");
        can_save = false;
        menu::update();
        require(menu::targets().size() == 4, "Unavailable Save remained a navigation target");
        fixture.click(menu::Surface::button(native_button, 0));
        require(save_requests == 1 && menu::expanded(), "Unavailable Save was activated");
        can_save = options.dialogue_transcript = true;
        menu::update();

        fixture.point(menu::Surface::button(native_button, 0));
        require(fixture.send(WM_LBUTTONDOWN), "Toolbar leaked a held action press");
        fixture.point(menu::Surface::button(native_button, 1));
        require(fixture.send(WM_LBUTTONUP) && save_requests == 1 && load_requests == 1,
                "Release over another action activated the original press");
        require(fixture.send(WM_KEYDOWN, VK_ESCAPE) && !menu::expanded(),
                "Escape did not dismiss the expanded menu");
        require(!fixture.send(WM_KEYDOWN, VK_ESCAPE), "Collapsed menu swallowed native Escape");

        fixture.expand();
        fixture.point(menu::Surface::button(native_button, 0));
        require(fixture.send(WM_LBUTTONDBLCLK), "Toolbar leaked a double-click press");
        require(fixture.send(WM_KEYDOWN, VK_ESCAPE), "Escape failed during a held press");
        require(fixture.send(WM_LBUTTONUP) && !fixture.send(WM_LBUTTONUP) && save_requests == 1,
                "Dismissal lost the matching release or consumed an unrelated later release");

        fixture.expand();
        fixture.point(menu::Surface::button(native_button, 0));
        require(fixture.send(WM_LBUTTONDOWN), "Toolbar leaked the context-loss press");
        available = false;
        menu::update();
        require(!menu::expanded() && fixture.send(WM_LBUTTONUP) && save_requests == 1,
                "Context loss leaked a release or activated a stale action");

        fixture.expand();
        fixture.point(menu::Surface::button(native_button, 0));
        require(fixture.send(WM_LBUTTONDOWN), "Toolbar context-replacement press leaked");
        identity.object = reinterpret_cast<void*>(2);
        menu::update();
        require(!menu::expanded() && fixture.send(WM_LBUTTONUP) && save_requests == 1,
                "Same-bounds native context replacement retained a held toolbar action");
        identity = {};

        fixture.expand();
        fixture.point(menu::Surface::button(native_button, 0));
        require(fixture.send(WM_LBUTTONDOWN), "Toolbar leaked the focus-loss press");
        require(!fixture.send(WM_KILLFOCUS) && !menu::expanded() && fixture.send(WM_LBUTTONUP) &&
                    save_requests == 1,
                "Focus loss committed a stale action or lost mouse pairing");

        fixture.expand();
        fixture.point(menu::Surface::button(native_button, 0));
        require(fixture.send(WM_LBUTTONDOWN), "Toolbar press was not consumed");
        fixture.send(WM_KILLFOCUS);
        fixture.point({100, 100, 120, 120});
        require(!fixture.send(WM_LBUTTONDOWN) && !fixture.send(WM_LBUTTONUP),
                "An undelivered old release swallowed a fresh native scene click");

        for (const bool replace_context : {true, false}) {
            fixture.expand();
            fixture.click(menu::Surface::button(native_button, 4));
            if (replace_context) {
                identity.object = reinterpret_cast<void*>(1);
            } else {
                restore_ok = false;
            }
            foreground = true;
            POINT before{}, after{};
            GetCursorPos(&before);
            menu::update();
            require(GetCursorPos(&after) && before.x == after.x && before.y == after.y,
                    "Stale context or failed native restoration moved the cursor");
            foreground = false;
            restore_ok = true;
            identity = {};
        }

        for (const auto focus_message : {WM_KILLFOCUS, WM_ACTIVATEAPP}) {
            fixture.expand();
            fixture.click(menu::Surface::button(native_button, 4));
            require(!menu::expanded(), "Menu action did not dismiss the toolbar");
            const auto queries = foreground_queries;
            fixture.send(focus_message);
            menu::update();
            require(foreground_queries == queries,
                    "Focus loss retained a deferred native Menu request");
        }
        fixture.expand();
        fixture.click(menu::Surface::button(native_button, 4));
        const auto queries = foreground_queries;
        POINT before_cancel{}, after_cancel{};
        require(GetCursorPos(&before_cancel) != FALSE, "Cannot read cursor before Menu cancel");
        menu::update();
        require(GetCursorPos(&after_cancel) && before_cancel.x == after_cancel.x &&
                    before_cancel.y == after_cancel.y,
                "Canceled Menu action moved the user's cursor");
        require(foreground_queries == queries + 1,
                "Deferred Menu request did not check foreground ownership");
        menu::update();
        require(foreground_queries == queries + 1, "Canceled Menu request was retried");

        fixture.expand();
        fixture.click(menu::Surface::button(native_button, 3));
        require(settings_requests == 1 && !menu::expanded(),
                "Tweaks gear did not request settings");
        dialogue = true;
        can_save = false;
        fixture.expand();
        require(menu::targets().size() == 4 && corner_suppressed,
                "Dialogue hid the toolbar or enabled unavailable save/load actions");
        fixture.click(menu::Surface::button(native_button, 1));
        require(load_requests == 1 && menu::expanded(), "Dialogue enabled an unsafe direct load");
        fixture.click(menu::Surface::button(native_button, 2));
        require(transcript_requests == 2 && transcript_open,
                "Dialogue toolbar did not open the transcript");
        dialogue = false;

        browser = transcript_open = false;
        can_save = true;
        menu::dismiss();
        for (unsigned mask = 1; mask < 32; ++mask) {
            for (unsigned i = 0; i < 5; ++i) {
                options.quick_menu_items[i] = (mask & (1u << i)) != 0;
            }
            fixture.expand();
            const auto targets = menu::targets();
            unsigned count = 0;
            for (const auto item : options.quick_menu_items) {
                count += item;
            }
            require(targets.size() == count + 1, "Hidden item remained a navigation target");
            const auto anchor = menu::Surface::toggle(native_button);
            for (unsigned i = 0; i < count; ++i) {
                require(targets[i].left == anchor.left - static_cast<LONG>(count - i) * 28 &&
                            targets[i].right == targets[i].left + 28,
                        "Visible items did not pack together in their original order");
            }
            menu::dismiss();
        }
        options.quick_menu_items = {false, true, false, true, false};
        fixture.expand();
        const auto compact_load = menu::targets().front();
        const auto old_load_requests = load_requests;
        fixture.click(compact_load);
        require(load_requests == old_load_requests + 1 && browser,
                "Compacted Load position activated the wrong action");
        fixture.expand();
        const auto compact_tweaks = menu::targets()[1];
        fixture.click(compact_tweaks);
        require(settings_requests == 2, "Compacted gear did not open Tweaks");
        fixture.expand();
        fixture.point(menu::targets().front());
        require(fixture.send(WM_LBUTTONDOWN), "Compact action press leaked");
        options.quick_menu_items = {false, false, true, false, true};
        menu::update();
        require(!menu::expanded() && fixture.send(WM_LBUTTONUP) &&
                    load_requests == old_load_requests + 1,
                "Changing item visibility committed a stale press or leaked its release");
        options.quick_menu = false;
        menu::update();
        options.quick_menu = true;
        fixture.expand();
        require(menu::targets().size() == 3 && !options.quick_menu_items[0] &&
                    options.quick_menu_items[2],
                "Master toggle discarded the chosen submenu items");
        options.quick_menu_items.fill(false);
        menu::update(false);
        require(menu::targets().empty() && !corner_suppressed,
                "An empty toolbar retained native corner ownership in the background");
        menu::update();
        require(menu::targets().empty() && !corner_suppressed,
                "An empty toolbar exposed a useless toggle");
        options.quick_menu_items.fill(true);
        fixture.expand();
        fixture.click(menu::Surface::button(native_button, 4));
        const auto hidden_menu_queries = foreground_queries;
        options.quick_menu_items[4] = false;
        menu::update();
        require(foreground_queries == hidden_menu_queries,
                "Hiding Menu retained its deferred native action");

        require(!IsWindowVisible(fixture.window), "Menu input test exposed a visible window");
        menu::release();
        require(!corner_suppressed && menu::targets().empty() &&
                    menu::canvas(fixture.background) == fixture.background,
                "Menu release retained its input targets or canvas decorator");
        std::cout << "Gameplay toolbar input, drawing, pairing and deferred Menu cancel passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
