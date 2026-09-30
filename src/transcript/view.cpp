#include "view.h"
#include "page.h"
#include "capture.h"
#include "history.h"
#include "enhancements/game_ui.h"
#include "enhancements/controls.h"
#include "enhancements/quick_save.h"
#include "enhancements/scene_overlay.h"
#include "enhancements/ui/quick_menu.h"
#include "settings.h"
#include "game/render/native_render.h"
#include "saves/browser.h"
#include "playback/fast_forward_input.h"
#include <algorithm>
#include <memory>

namespace transcript {
namespace {
std::unique_ptr<Page> surface;
bool opened = false;
bool consumed_left = false;
enhancements::SceneOverlay scene_pause;
int pressed = -1;
std::size_t selected_page = 0;
std::vector<std::wstring> entries;
std::wstring note;

bool menu_visible() {
    return enhancements::game::input_vtable() == enhancements::game::edition().main_menu &&
           !enhancements::game::menu_confirmation_active() && !saves::browser_active();
}

void close() {
    opened = false;
    pressed = -1;
    entries.clear();
    scene_pause.end();
    native_game::invalidate_canvas();
}

void paint() {
    surface->draw(native_game::canvas_dc(), entries, note, selected_page);
    selected_page = std::min(selected_page, surface->pages() - 1);
    native_game::invalidate_canvas();
}

void open() {
    if (!available() || (!menu_visible() && !scene_pause.begin(false))) {
        return;
    }
    playback::suspend_fast_forward_input();
    enhancements::suspend_controller();
    enhancements::quick_menu::dismiss();
    entries.clear();
    for (const auto& entry : history().entries()) {
        entries.push_back(entry.kind == Kind::Choice ? L"Selected: " + entry.text : entry.text);
    }
    std::wstring executable(32768, L'\0');
    const auto length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length && length < executable.size()) {
        executable.resize(length);
        note = edition_note(std::filesystem::path(executable).parent_path());
    }
    if (history().evicted()) {
        note = L"Older entries discarded. " + note;
    }
    selected_page = 0;
    opened = true;
    paint();
    selected_page = surface->pages() - 1;
    paint();
}

HDC canvas(HDC native) {
    return opened && surface ? surface->dc() : enhancements::quick_menu::canvas(native);
}

int hit(POINT point) {
    if (PtInRect(&previous_button, point)) {
        return 0;
    }
    if (PtInRect(&next_button, point)) {
        return 1;
    }
    if (PtInRect(&close_button, point)) {
        return 2;
    }
    return -1;
}

void turn(int direction) {
    if (direction < 0 && selected_page > 0) {
        --selected_page;
    }
    if (direction > 0 && selected_page + 1 < surface->pages()) {
        ++selected_page;
    }
    paint();
}
}

bool available() {
    return settings().dialogue_transcript && surface && native_game::native_render_available();
}

bool show() {
    open();
    return opened;
}

bool active() {
    return opened;
}

void update() {
    if (!surface && native_game::native_render_available()) {
        try {
            surface = std::make_unique<Page>();
            native_game::set_canvas_overlay(canvas);
            native_game::invalidate_canvas();
        } catch (...) {
            return;
        }
    }
    if (opened && (!settings().dialogue_transcript ||
                   (scene_pause.active() ? !scene_pause.valid() : !menu_visible()))) {
        close();
    }
}

bool message(HWND window, UINT message_id, WPARAM value, LPARAM data) {
    if (message_id == WM_LBUTTONUP && consumed_left && !opened) {
        consumed_left = false;
        return true;
    }
    if (!available()) {
        return false;
    }
    if (message_id == WM_KILLFOCUS || (message_id == WM_ACTIVATEAPP && !value)) {
        pressed = -1;
        return false;
    }
    if (!opened) {
        if (!menu_visible() && !enhancements::scene_overlay_available(false)) {
            return false;
        }
        if (message_id == WM_KEYDOWN && value == VK_F8) {
            if (!(data & (1L << 30))) {
                open();
            }
            return true;
        }
        return false;
    }
    if (message_id == WM_KEYDOWN) {
        if (data & (1L << 30)) {
            return true;
        }
        if (value == VK_ESCAPE || value == VK_BACK || value == VK_F8 || value == VK_RETURN) {
            close();
        } else if (value == VK_LEFT || value == VK_UP || value == VK_PRIOR) {
            turn(-1);
        } else if (value == VK_RIGHT || value == VK_DOWN || value == VK_NEXT || value == VK_SPACE) {
            turn(1);
        } else if (value == VK_HOME || value == VK_END) {
            selected_page = value == VK_HOME ? 0 : surface->pages() - 1;
            paint();
        }
        return true;
    }
    if (message_id == WM_MOUSEWHEEL) {
        turn(GET_WHEEL_DELTA_WPARAM(value) > 0 ? -1 : 1);
        return true;
    }
    if (message_id == WM_RBUTTONDOWN) {
        close();
        return true;
    }
    if (message_id == WM_LBUTTONDOWN || message_id == WM_LBUTTONDBLCLK ||
        message_id == WM_LBUTTONUP) {
        consumed_left = message_id != WM_LBUTTONUP;
        POINT point{};
        if (GetCursorPos(&point) && ScreenToClient(window, &point)) {
            const auto item = hit(point);
            if (message_id != WM_LBUTTONUP) {
                pressed = item;
            } else {
                const auto selected = pressed;
                pressed = -1;
                if (item == selected && item >= 0) {
                    if (item == 2) {
                        close();
                    } else {
                        turn(item == 0 ? -1 : 1);
                    }
                }
            }
        }
        return true;
    }
    return (message_id >= WM_KEYFIRST && message_id <= WM_KEYLAST) ||
           (message_id >= WM_MOUSEFIRST && message_id <= WM_MOUSELAST) ||
           message_id == WM_SYSKEYDOWN || message_id == WM_SYSKEYUP;
}

void release() {
    opened = false;
    consumed_left = false;
    scene_pause.end(true);
    native_game::set_canvas_overlay(nullptr);
    surface.reset();
    entries.clear();
    pressed = -1;
}
}
