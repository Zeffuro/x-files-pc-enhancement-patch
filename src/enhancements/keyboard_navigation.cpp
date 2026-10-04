#include "keyboard_navigation.h"
#include "controls.h"
#include "dialogue.h"
#include "focus.h"
#include "game_ui.h"
#include "inventory.h"
#include "screens.h"

#include <algorithm>
#include <span>

namespace enhancements {
namespace {

bool activate_group(HWND window, std::span<const RECT> items) {
    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) {
        return true;
    }
    const auto selected = std::find_if(items.begin(), items.end(),
                                       [&](const RECT& item) { return PtInRect(&item, cursor); });
    if (selected != items.end()) {
        point_controller(window, *selected, true);
    } else if (!items.empty()) {
        // Establish focus before Enter can activate an unselected response.
        point_controller(window, items.front());
    }
    return true;
}

}

bool navigate_keyboard(HWND window, WPARAM key) {
    const int horizontal = (key == VK_RIGHT) - (key == VK_LEFT);
    const int vertical = (key == VK_DOWN) - (key == VK_UP);
    if ((!horizontal && !vertical && key != VK_RETURN && key != VK_BACK && key != VK_TAB) ||
        !game_is_foreground(window) || (GetKeyState(VK_CONTROL) & 0x8000) ||
        (GetKeyState(VK_MENU) & 0x8000) || (GetKeyState(VK_LWIN) & 0x8000) ||
        (GetKeyState(VK_RWIN) & 0x8000)) {
        return false;
    }
    if (navigate_screen(window, horizontal, vertical, key == VK_RETURN, key == VK_BACK,
                        key == VK_TAB, true)) {
        return true;
    }
    if (!game::input_vtable() && game::script_controls().text_input) {
        return false;
    }
    if (key == VK_TAB) {
        return focus_conversation_evidence(window) || navigate_emotions(window, 0, true) ||
               focus_inventory(window);
    }
    if (key == VK_BACK) {
        return leave_inventory(window) || close_dialogue(window);
    }
    if (key == VK_RETURN) {
        if (inventory_focused(window)) {
            return activate_group(window, inventory_bounds(game::current_view()));
        }
        if (const auto* dialogue = current_dialogue()) {
            std::vector<RECT> items(dialogue->choices.begin(),
                                    dialogue->choices.begin() + dialogue->count);
            const auto evidence = conversation_evidence();
            items.insert(items.end(), evidence.begin(), evidence.end());
            items.push_back(dialogue->talk);
            items.push_back(dialogue->history);
            return activate_group(window, items);
        }
        const auto emotions = game::emotion_targets();
        return !emotions.empty() && activate_group(window, emotions);
    }
    return navigate_emotions(window, horizontal ? horizontal : vertical, false) ||
           navigate_inventory(window, horizontal ? horizontal : vertical) ||
           navigate_dialogue(window, vertical, horizontal);
}

}
