#include "continue_menu.h"
#include "enhancements/game_ui.h"
#include "enhancements/quick_save.h"
#include "enhancements/controls.h"
#include "saves/browser.h"
#include "saves/recent.h"
#include "settings.h"
#include "transcript/view.h"
#include "runtime.h"

namespace enhancements {
namespace {
constexpr RECT return_button{472, 225, 638, 261};
bool pressed = false;

bool startup_menu() {
    const auto image = game::executable_image();
    return settings().continue_latest && image &&
           game::input_vtable() == game::edition().main_menu &&
           !*reinterpret_cast<int*>(image + game::edition().session_active) &&
           !game::menu_confirmation_active() && !saves::browser_active() && !transcript::active() &&
           checkpoint_available();
}

std::optional<saves::Slot> latest_save() {
    return saves::newest_save(save_game_root(),
                              game::edition().application != game::cd.application);
}
}

bool continue_message(HWND window, UINT message, WPARAM value, LPARAM) {
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !value)) {
        pressed = false;
        return false;
    }
    if (message != WM_LBUTTONDOWN && message != WM_LBUTTONDBLCLK && message != WM_LBUTTONUP) {
        return false;
    }
    if (message == WM_LBUTTONUP) {
        const bool consumed = pressed;
        pressed = false;
        if (!consumed) {
            return false;
        }
        POINT point{};
        if (startup_menu() && GetCursorPos(&point) && ScreenToClient(window, &point) &&
            PtInRect(&return_button, point)) {
            try {
                if (const auto saved = latest_save()) {
                    suspend_controller();
                    load_checkpoint(window, saved->file);
                }
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(), "The X-Files Return", MB_OK | MB_ICONERROR);
            }
        }
        return true;
    }
    pressed = false;
    POINT point{};
    if (startup_menu() && GetCursorPos(&point) && ScreenToClient(window, &point) &&
        PtInRect(&return_button, point)) {
        try {
            // Leave native Return in charge when there is no compatible saved game.
            pressed = latest_save().has_value();
        } catch (const std::exception& error) {
            trace_value(error.what(), 0);
        }
    }
    return pressed;
}

void release_continue_menu() {
    pressed = false;
}
}
