#include "documents.h"
#include "document_text.h"
#include "game_resources.h"
#include "ui/document_page.h"
#include "ui/quick_menu.h"
#include "scene_overlay.h"
#include "controls.h"
#include "text_entry.h"
#include "settings.h"
#include "game/render/native_render.h"
#include "saves/browser.h"
#include "transcript/view.h"
#include "playback/fast_forward_input.h"
#include <array>
#include <memory>

namespace enhancements::documents {
namespace {
std::unique_ptr<Page> page;
std::optional<Document> document;
SceneOverlay pause;
bool opened = false;
bool readable = false;
bool consumed_left = false;
int pressed = -1;
int text_size = 20;
int scroll = 0;
int wheel = 0;
bool wheel_zoom = false;
constexpr std::array buttons{smaller_button, larger_button, up_button, down_button, done_button};

unsigned code_page() {
    return game::edition().text == native_game::profile_cd_10020.text ? 932u : 1252u;
}

std::optional<Document> current() {
    if (!settings().readable_documents || game::input_vtable() ||
        game::menu_confirmation_active() || saves::browser_active() || transcript::active() ||
        text_entry_busy()) {
        return std::nullopt;
    }
    return current_document(game::script_controls(), code_page());
}

void close() {
    opened = false;
    pressed = -1;
    wheel = 0;
    wheel_zoom = false;
    document.reset();
    pause.end();
    native_game::invalidate_canvas();
}

HDC canvas(HDC background) {
    if (!page || !opened) {
        return background;
    }
    page->draw(background, &*document, text_size, scroll);
    scroll = page->scroll();
    return page->dc();
}

void redraw() {
    native_game::invalidate_canvas();
}

void turn(int movement) {
    scroll = std::clamp(scroll + movement, 0, page->maximum());
    redraw();
}

int hit(HWND window) {
    POINT point{};
    if (!GetCursorPos(&point) || !ScreenToClient(window, &point)) {
        return -1;
    }
    if (!opened) {
        const auto controls = game::script_controls();
        const auto snapshot = current_document(controls, code_page());
        if (!snapshot || !PtInRect(&snapshot->bounds, point) ||
            std::any_of(controls.buttons.begin(), controls.buttons.end(),
                        [&](const RECT& bounds) { return PtInRect(&bounds, point); })) {
            return -1;
        }
        return 5;
    }
    for (unsigned index = 0; index < buttons.size(); ++index) {
        if (PtInRect(&buttons[index], point)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void activate(int item) {
    if (item == 5) {
        show();
    } else if (item == 4) {
        close();
    } else if (item == 0 || item == 1) {
        text_size = std::clamp(text_size + (item == 0 ? -2 : 2), 16, 28);
        redraw();
    } else if (item == 2 || item == 3) {
        turn(item == 2 ? -100 : 100);
    }
}
}

bool available() {
    return page && (opened || readable) && native_game::native_render_available() &&
           current().has_value() && (opened || scene_overlay_available(false));
}

bool show() {
    if (opened) {
        return true;
    }
    auto snapshot = current();
    if (!page || !snapshot || !pause.begin(false)) {
        return false;
    }
    playback::suspend_fast_forward_input();
    suspend_controller();
    quick_menu::dismiss();
    document = std::move(snapshot);
    opened = true;
    scroll = 0;
    wheel = 0;
    wheel_zoom = false;
    redraw();
    return true;
}

bool active() {
    return opened;
}

void update(HWND, bool enabled) {
    if (!page && native_game::native_render_available()) {
        try {
            page = std::make_unique<Page>();
            native_game::set_canvas_reader(canvas);
        } catch (...) {
            return;
        }
    }
    if (opened && (!settings().readable_documents || !pause.valid() || current() != document)) {
        close();
    }
    readable = enabled && page && current() && scene_overlay_available(false);
}

bool message(HWND window, UINT id, WPARAM value, LPARAM data) {
    if (id == WM_SYSKEYDOWN || id == WM_SYSKEYUP || id == WM_SYSCHAR || id == WM_SYSDEADCHAR ||
        ((id == WM_KEYDOWN || id == WM_KEYUP) &&
         ((data & (1L << 29)) || GetKeyState(VK_MENU) < 0))) {
        return false;
    }
    if (id == WM_LBUTTONUP && consumed_left && !opened) {
        consumed_left = false;
        const auto previous = pressed;
        pressed = -1;
        if (previous == 5 && available() && !quick_menu::expanded() && hit(window) == 5) {
            show();
        }
        return true;
    }
    if (id == WM_KILLFOCUS || (id == WM_ACTIVATEAPP && !value)) {
        pressed = -1;
        wheel = 0;
        wheel_zoom = false;
        return false;
    }
    if (id == WM_LBUTTONDOWN || id == WM_LBUTTONDBLCLK) {
        consumed_left = false;
        pressed = -1;
    }
    if (!opened && (!available() || quick_menu::expanded())) {
        return false;
    }
    if (opened && id == WM_KEYDOWN) {
        if (value == VK_ESCAPE || value == VK_BACK || value == VK_RETURN) {
            close();
        } else if (value == VK_UP) {
            turn(-text_size * 3);
        } else if (value == VK_DOWN) {
            turn(text_size * 3);
        } else if (value == VK_PRIOR) {
            turn(-300);
        } else if (value == VK_NEXT || value == VK_SPACE) {
            turn(300);
        } else if (value == VK_HOME || value == VK_END) {
            scroll = value == VK_HOME ? 0 : page->maximum();
            redraw();
        } else if (value == VK_OEM_PLUS || value == VK_ADD) {
            activate(1);
        } else if (value == VK_OEM_MINUS || value == VK_SUBTRACT) {
            activate(0);
        }
        return true;
    }
    if (opened && id == WM_MOUSEWHEEL) {
        const bool zoom = (GET_KEYSTATE_WPARAM(value) & MK_CONTROL) != 0;
        if (zoom != wheel_zoom) {
            wheel = 0;
            wheel_zoom = zoom;
        }
        wheel += GET_WHEEL_DELTA_WPARAM(value);
        const auto steps = wheel / WHEEL_DELTA;
        wheel %= WHEEL_DELTA;
        if (zoom) {
            text_size = std::clamp(text_size + steps * 2, 16, 28);
            redraw();
        } else {
            turn(-steps * text_size * 3);
        }
        return true;
    }
    if (opened && id == WM_RBUTTONDOWN) {
        close();
        return true;
    }
    if (id == WM_LBUTTONDOWN || id == WM_LBUTTONDBLCLK || id == WM_LBUTTONUP) {
        const auto item = hit(window);
        if (!opened && (item != 5 || id == WM_LBUTTONUP)) {
            return false;
        }
        consumed_left = id != WM_LBUTTONUP;
        if (id != WM_LBUTTONUP) {
            pressed = item;
        } else {
            const auto previous = pressed;
            pressed = -1;
            if (item >= 0 && item == previous) {
                activate(item);
            }
        }
        return true;
    }
    return opened &&
           ((id >= WM_KEYFIRST && id <= WM_KEYLAST) || (id >= WM_MOUSEFIRST && id <= WM_MOUSELAST));
}

void release(bool abandon) {
    opened = false;
    readable = false;
    consumed_left = false;
    pause.end(abandon);
    native_game::set_canvas_reader(nullptr);
    page.reset();
    document.reset();
    pressed = -1;
    wheel = 0;
    wheel_zoom = false;
}
}
