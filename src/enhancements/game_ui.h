#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <algorithm>
#include "edition.h"
#include "game/layouts/ui.h"

namespace enhancements::game {

using native_game::Application;
using native_game::ChildView;
using native_game::ChoiceList;
using native_game::Container;
using native_game::ConversationIcons;
using native_game::Inventory;
using native_game::InventoryIcon;
using native_game::InventoryItem;
using native_game::List;
using native_game::MainView;
using native_game::Rectangle;

MainView* current_view();
std::byte* executable_image();
void* current_input();
std::uintptr_t input_vtable();
bool menu_confirmation_active();
bool saving_available();
std::vector<RECT> modal_buttons();

struct ScriptControls {
    struct EventTarget {
        RECT bounds;
        unsigned id;
        unsigned events;
    };

    struct Text {
        RECT bounds;
        std::string value;
    };

    std::vector<unsigned> resources;
    std::vector<RECT> buttons;
    std::vector<RECT> hover_buttons;
    std::vector<RECT> acknowledgement_buttons;
    std::vector<RECT> dialog_buttons;
    std::vector<RECT> dialog_fields;
    bool script_dialog = false;
    bool text_input = false;
    std::vector<RECT> text;
    std::vector<Text> fields;
    std::vector<Text> document_text;
    std::vector<EventTarget> event_targets;
};

ScriptControls script_controls();
bool activate_script_button(unsigned resource, const RECT& bounds);
bool world_navigation_available();
RECT scene_bounds();

enum class Interaction {
    unknown,
    click,
    item,
    view,
    talk,
    use,
    move_left,
    move_right,
    move_forward,
    move_back
};

constexpr int movement_direction(Interaction interaction) {
    switch (interaction) {
        case Interaction::move_left:
            return 1;
        case Interaction::move_right:
            return 2;
        case Interaction::move_forward:
            return 3;
        case Interaction::move_back:
            return 4;
        default:
            return 0;
    }
}

struct WorldTarget {
    RECT bounds;
    RECT exposed;
    std::uintptr_t identity;
    bool navigation;
    Interaction interaction = Interaction::unknown;
};

std::vector<WorldTarget> world_targets(bool include_occluded = false);
std::vector<RECT> world_hotspots(bool navigation_only, bool include_occluded = false);
std::vector<RECT> emotion_targets();
std::vector<RECT> aiming_targets();
bool movie_skippable();

}
