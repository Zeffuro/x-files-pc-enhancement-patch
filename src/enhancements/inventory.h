#pragma once

#include "game_ui.h"

#include <vector>
#include <optional>

namespace enhancements {

std::vector<RECT> inventory_bounds(const game::MainView* view);
std::optional<RECT> inventory_item_bounds(const game::MainView* view, unsigned resource);
bool focus_inventory_item(HWND window, unsigned resource);
bool focus_inventory(HWND window);
bool leave_inventory(HWND window);
bool inventory_focused(HWND window);
bool navigate_inventory(HWND window, int direction);
void clear_inventory_focus();

}
