#include "browser.h"
#include "browser_state.h"
#include "recent.h"
#include "artwork.h"
#include "enhancements/game_ui.h"
#include "enhancements/quick_save.h"
#include "enhancements/scene_overlay.h"
#include "enhancements/controls.h"
#include "enhancements/keyboard_navigation.h"
#include "game/render/native_render.h"
#include "runtime.h"
#include "settings.h"
#include "playback/inspection.h"
#include <algorithm>
#include <cwctype>

namespace saves {
namespace {
std::shared_ptr<Browser> browser;
bool committing = false;
bool consumed_left = false;
enhancements::NavigationKeys naming_keys;
enhancements::SceneOverlay scene_pause;
Thumbnail last_scene;
SceneReference last_reference;
ULONGLONG last_capture = 0;
int pressed = -1, menu_pressed = -1;

void repaint() {
    if (browser) {
        draw_browser(*browser);
    }
    native_game::invalidate_canvas();
}

void close() {
    native_game::set_canvas_source(nullptr);
    browser.reset();
    scene_pause.end();
    pressed = -1;
    native_game::invalidate_canvas();
}

void select(unsigned index) {
    browser->selection = index;
    browser->focus = static_cast<int>(index);
    browser->name = browser->slots[index].name;
    browser->confirm = false;
    browser->deleting = false;
    browser->naming = false;
    browser->status.clear();
}

void change_page(int direction) {
    change_browser_page(*browser, direction);
}

void open(bool saving) {
    auto state = std::make_unique<Browser>();
    std::wstring executable(32768, L'\0');
    const auto length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) {
        throw std::runtime_error("Cannot locate the game folder");
    }
    executable.resize(length);
    state->root = std::filesystem::path(executable).parent_path();
    state->text = load_browser_text(state->root);
    state->saving = saving;
    state->scene = last_scene;
    state->scene_reference = last_reference;
    state->legacy = read_catalog(state->root);
    std::erase_if(state->legacy.entries, [](const Entry& entry) {
        auto name = entry.path.filename().wstring();
        std::transform(name.begin(), name.end(), name.begin(), std::towlower);
        return name == L"quicksave.x" || name == L"quicksave.previous.x";
    });
    if (!saving) {
        state->quicksaves = read_quicksaves(state->root);
        state->autosaves = read_autosaves(state->root);
    }
    load_browser_art(*state);
    load_browser_page(*state);
    draw_browser(*state);
    browser = std::move(state);
    trace_value("save_browser_mode", saving ? 1 : 2);
    native_game::set_canvas_source(browser_canvas);
    native_game::invalidate_canvas();
}

void commit(HWND window) {
    const auto state = browser;
    const auto chosen = state->slots[state->selection];
    if ((state->saving || state->deleting) && state->category != BrowserCategory::manual) {
        return;
    }
    if (state->deleting && state->confirm) {
        delete_slot(state->root, chosen.number);
        load_browser_page(*state);
        state->deleting = false;
        state->focus = static_cast<int>(state->selection);
        return;
    }
    if (!browser->saving && !chosen.readable) {
        return;
    }
    if (!browser->confirm && (!browser->saving || chosen.occupied)) {
        browser->confirm = true;
        browser->focus = 10;
        browser->naming = false;
        browser->status =
            browser->saving ? browser->text.overwrite_prompt : browser->text.load_warning;
        return;
    }

    struct CommitGuard {
        CommitGuard() {
            committing = true;
        }

        ~CommitGuard() {
            committing = false;
        }
    } guard;

    if (state->saving) {
        const auto temporary = state->root / L"saves" /
                               (L"BROWSER." + std::to_wstring(GetCurrentProcessId()) + L"." +
                                std::to_wstring(GetTickCount64()) + L".x");
        if (std::filesystem::exists(temporary)) {
            throw std::runtime_error("Try saving again");
        }

        struct Cleanup {
            std::filesystem::path file;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove(file, error);
            }
        } cleanup{temporary};

        enhancements::export_save(temporary);
        if (browser != state) {
            return;
        }
        write_slot(state->root, chosen.number, state->name, temporary, state->scene);
        const auto saved = read_slot(state->root, chosen.number);
        try {
            write_scene_reference(saved.file, state->scene_reference);
        } catch (const std::exception&) {
            trace_value("save_browser_preview_write_failed", chosen.number);
        }
        trace_value("save_browser_saved_slot", chosen.number);
        load_browser_page(*state);
        state->status = state->text.saved;
    } else {
        trace_value("save_browser_load_slot", chosen.number);
        enhancements::load_checkpoint(window, chosen.file);
        if (enhancements::checkpoint_load_pending()) {
            scene_pause.end(true);
        }
        if (browser == state) {
            close();
        }
    }
}

int hit(POINT p) {
    if (browser->keyboard) {
        for (unsigned i = 0; i < name_key_count; ++i) {
            const auto bounds = name_key_rect(i);
            if (PtInRect(&bounds, p)) {
                return 100 + static_cast<int>(i);
            }
        }
        return -1;
    }
    for (int i = 0; i <= 12; ++i) {
        const auto bounds = control_rect(i);
        if (control_enabled(*browser, i) && PtInRect(&bounds, p)) {
            return i;
        }
    }
    return -1;
}

void action(HWND window, int item) {
    if (item >= 100 && item < 100 + static_cast<int>(name_key_count)) {
        const auto key = static_cast<unsigned>(item - 100);
        browser->key = key;
        if (key < 40 && browser->name.size() < 80) {
            browser->name += name_keys[key];
        } else if (key == 40 && browser->name.size() < 80) {
            browser->name += L' ';
        } else if (key == 41 && !browser->name.empty()) {
            browser->name.pop_back();
        } else if (key == 42) {
            browser->name.clear();
        } else if (key == 43) {
            browser->keyboard = browser->naming = false;
        }
        return;
    }
    if (!control_enabled(*browser, item)) {
        return;
    }
    browser->focus = item;
    if (item >= 0 && item < 6) {
        select(static_cast<unsigned>(item));
    } else if (item == 6 || item == 7) {
        change_page(item == 6 ? -1 : 1);
    } else if (item == 8 && !browser->saving) {
        cycle_browser_category(*browser);
    } else if (item == 9 && browser->saving) {
        browser->naming = true;
        browser->keyboard = true;
        browser->confirm = false;
    } else if (item == 10) {
        if (browser->confirm) {
            browser->confirm = false;
            browser->deleting = false;
            browser->status.clear();
        } else {
            close();
        }
    } else if (item == 11) {
        commit(window);
    } else if (item == 12) {
        browser->deleting = browser->confirm = true;
        browser->focus = 10;
        browser->status = browser->text.confirm_delete;
    }
}

void paste(HWND window) {
    if (!OpenClipboard(window)) {
        return;
    }
    if (const auto handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto data = static_cast<const wchar_t*>(GlobalLock(handle))) {
            const auto length = std::min<std::size_t>(GlobalSize(handle) / sizeof(wchar_t), 81);
            for (std::size_t i = 0; i < length && data[i] && browser->name.size() < 80; ++i) {
                if (data[i] >= L' ' && data[i] != 0x7f) {
                    browser->name += data[i];
                }
            }
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
}
}

bool browser_active() {
    return browser != nullptr;
}

Thumbnail browser_scene_thumbnail() {
    return last_scene;
}

SceneReference browser_scene_reference() {
    return last_reference;
}

bool show_browser(bool saving) {
    if (browser || !settings().save_browser || !native_game::native_render_available() ||
        !(saving ? enhancements::export_save_available() : enhancements::checkpoint_available())) {
        return false;
    }
    const bool menu = enhancements::game::input_vtable() == enhancements::game::edition().main_menu;
    if (!menu && !scene_pause.begin()) {
        return false;
    }
    try {
        enhancements::suspend_controller();
        open(saving);
        return true;
    } catch (const std::exception& error) {
        scene_pause.end();
        trace_value(error.what(), 0);
        return false;
    }
}

HDC browser_canvas(HDC native) {
    return browser ? browser->output.dc : native;
}

void update_browser(HWND) {
    if (browser && scene_pause.active() && !scene_pause.valid()) {
        scene_pause.end(true);
        close();
    }
    if (browser) {
        const auto target = browser->keyboard || browser->confirm ? -1
                            : browser->hover >= 0                 ? browser->hover
                            : browser->focus < 6                  ? browser->focus
                                                                  : -1;
        if (target != browser->preview_slot) {
            browser->preview.reset();
            browser->preview_slot = target;
            browser->preview_started = GetTickCount64();
            browser->preview_failed = false;
        }
        if (target >= 0 && !browser->preview_failed &&
            GetTickCount64() - browser->preview_started >= 250 &&
            GetTickCount64() - browser->preview_painted >= 50) {
            try {
                browser->preview_painted = GetTickCount64();
                if (!browser->preview) {
                    const auto reference = read_scene_reference(browser->slots[target].file);
                    if (!reference.track) {
                        browser->preview_failed = true;
                        return;
                    }
                    browser->preview = std::make_unique<ScenePreview>(browser->root, reference);
                }
                browser->preview->update(GetTickCount64() - browser->preview_started - 250);
                repaint();
            } catch (const std::exception&) {
                browser->preview.reset();
                browser->preview_failed = true;
            }
        }
        return;
    }
    if (GetTickCount64() - last_capture < 500 || !enhancements::game::saving_available() ||
        enhancements::game::input_vtable() != 0) {
        return;
    }
    last_capture = GetTickCount64();
    try {
        if (const auto dc = native_game::canvas_dc()) {
            last_scene = capture_scene(dc, enhancements::game::scene_bounds());
            last_reference = {};
            std::uint64_t newest = 0;
            const auto bounds = enhancements::game::scene_bounds();
            const auto area = (bounds.right - bounds.left) * (bounds.bottom - bounds.top);
            for (const auto& movie : playback::inspect_movies()) {
                if (!movie.image || !movie.video || movie.last_draw < newest ||
                    movie.width * movie.height < area * 3 / 4) {
                    continue;
                }
                SceneReference reference{movie.path, movie.image->track, movie.image->sample,
                                         movie.playing && !media::navigation_archive(movie.path),
                                         static_cast<std::uint64_t>(std::max(0, movie.time))};
                if (!valid_scene_reference(reference)) {
                    reference.motion = false;
                }
                if (valid_scene_reference(reference)) {
                    last_reference = reference;
                    newest = movie.last_draw;
                }
            }
        }
    } catch (const std::exception&) {
        last_scene = {};
        last_reference = {};
    }
}

bool browser_message(HWND window, UINT message, WPARAM value, LPARAM data, BrowserInput input) {
    try {
        if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !value)) {
            pressed = menu_pressed = -1;
            naming_keys.reset();
            return false;
        }
        if (input == BrowserInput::keyboard && naming_keys.owns(message, value, data)) {
            return true;
        }
        if (message == WM_LBUTTONUP && consumed_left && !browser) {
            consumed_left = false;
            return true;
        }
        if (committing) {
            return message == WM_CLOSE || message == WM_SYSCOMMAND ||
                   (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
                   (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST);
        }
        if (!browser) {
            if ((message != WM_LBUTTONDOWN && message != WM_LBUTTONUP) ||
                !settings().save_browser || !native_game::native_render_available() ||
                enhancements::game::input_vtable() != enhancements::game::edition().main_menu ||
                enhancements::game::menu_confirmation_active()) {
                return false;
            }
            POINT point{};
            if (!GetCursorPos(&point) || !ScreenToClient(window, &point)) {
                return false;
            }
            const RECT save{472, 175, 639, 220}, load{472, 125, 639, 170};
            const auto item = PtInRect(&save, point) && enhancements::export_save_available() ? 0
                              : PtInRect(&load, point)                                        ? 1
                                                                                              : -1;
            if (message == WM_LBUTTONDOWN) {
                menu_pressed = item;
            }
            if (menu_pressed < 0) {
                return false;
            }
            if (message == WM_LBUTTONUP) {
                const auto selected = menu_pressed;
                menu_pressed = -1;
                if (item == selected) {
                    open(item == 0);
                }
            }
            return true;
        }
        if (message == WM_SETCURSOR) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return true;
        }
        if (message == WM_MOUSEMOVE) {
            POINT point{};
            if (GetCursorPos(&point) && ScreenToClient(window, &point)) {
                const auto item = hit(point);
                browser->hover = item >= 0 && item < 6 ? item : -1;
            }
            return true;
        }
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP) {
            consumed_left = message != WM_LBUTTONUP;
            POINT point{};
            if (GetCursorPos(&point) && ScreenToClient(window, &point)) {
                const auto item = hit(point);
                if (message != WM_LBUTTONUP) {
                    pressed = item;
                } else {
                    const auto selected = pressed;
                    pressed = -1;
                    if (item == selected) {
                        action(window, item);
                    }
                }
            }
            repaint();
            return true;
        }
        if (message == WM_MOUSEWHEEL) {
            if (!browser->keyboard && !browser->confirm) {
                change_page(GET_WHEEL_DELTA_WPARAM(value) > 0 ? -1 : 1);
            }
            repaint();
            return true;
        }
        if (message == WM_KEYDOWN) {
            browser->hover = -1;
            if (browser->keyboard && value >= VK_LEFT && value <= VK_DOWN) {
                const int step = value == VK_LEFT    ? -1
                                 : value == VK_RIGHT ? 1
                                 : value == VK_UP    ? -10
                                                     : 10;
                browser->key = static_cast<unsigned>(
                    (static_cast<int>(browser->key) + step + name_key_count) % name_key_count);
            } else if (browser->keyboard && value == VK_RETURN) {
                action(window, 100 + (input == BrowserInput::controller ? browser->key
                                                                        : name_key_count - 1));
                if (input == BrowserInput::keyboard) {
                    naming_keys.consume(VK_RETURN);
                }
            } else if (browser->keyboard && value == VK_ESCAPE) {
                browser->keyboard = browser->naming = false;
            } else if (value == VK_ESCAPE) {
                action(window, 10);
            } else if (!browser->confirm && !browser->keyboard &&
                       (value == VK_PRIOR || value == VK_NEXT)) {
                change_page(value == VK_PRIOR ? -1 : 1);
            } else if ((value == VK_HOME || value == VK_END) && !browser->naming &&
                       !browser->confirm) {
                change_page(value == VK_HOME ? -static_cast<int>(slot_pages)
                                             : static_cast<int>(slot_pages));
            } else if (browser->keyboard && value == VK_TAB) {
                browser->key = (browser->key + 1) % name_key_count;
            } else if (value == VK_TAB) {
                cycle_browser_focus(*browser, (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
                if (browser->focus < 6) {
                    select(static_cast<unsigned>(browser->focus));
                }
            } else if (value == VK_RETURN) {
                action(window, browser->focus < 6 ? 11 : browser->focus);
            } else if (browser->naming && value == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                paste(window);
            } else if (value >= VK_LEFT && value <= VK_DOWN && !browser->naming) {
                move_browser_focus(*browser,
                                   value == VK_LEFT    ? -1
                                   : value == VK_RIGHT ? 1
                                                       : 0,
                                   value == VK_UP     ? -1
                                   : value == VK_DOWN ? 1
                                                      : 0);
                if (browser->focus < 6) {
                    select(static_cast<unsigned>(browser->focus));
                }
            }
            repaint();
            return true;
        }
        if (message == WM_CHAR) {
            if (browser->naming) {
                if (value == VK_BACK && !browser->name.empty()) {
                    browser->name.pop_back();
                } else if (value >= 32 && value != 127 && value <= 0xffff &&
                           browser->name.size() < 80) {
                    browser->name += static_cast<wchar_t>(value);
                }
                repaint();
            }
            return true;
        }
        return message == WM_KEYUP || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ||
               message == WM_MOUSEMOVE || message == WM_LBUTTONDBLCLK;
    } catch (const std::exception& error) {
        if (browser) {
            const std::string reason = error.what();
            browser->status.assign(reason.begin(), reason.end());
            browser->confirm = false;
            browser->deleting = false;
            repaint();
        } else {
            MessageBoxA(window, error.what(), "Saved games", MB_OK | MB_ICONERROR);
        }
        return true;
    }
}

void release_browser() {
    scene_pause.end(true);
    native_game::set_canvas_source(nullptr);
    browser.reset();
    last_scene = {};
    last_reference = {};
    menu_pressed = pressed = -1;
    consumed_left = false;
    naming_keys.reset();
}
}
