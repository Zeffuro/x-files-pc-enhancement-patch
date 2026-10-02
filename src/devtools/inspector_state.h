#pragma once
#include <commctrl.h>

namespace devtools::inspector {
void create_state_controls();
void layout_state_controls(int width, int height);
void show_state_controls(bool visible);
void update_game_state_view();
void update_state_selection();
void update_state_history_view();
void update_state_history_selection();
void copy_state_history(bool selected_only = false);
bool state_command(int id, int event);
bool state_notify(const NMLISTVIEW& change);
}
