#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <vector>

namespace devtools {
struct StateGroup {
    std::wstring name;
    std::vector<std::wstring> values;
};

struct GameSnapshot {
    std::wstring text;
    std::vector<RECT> targets;
    std::vector<StateGroup> groups;
};

GameSnapshot inspect_game(bool include_story = true);
void show_hotspots(HWND game, bool enabled, const std::vector<RECT>& targets);
void release_hotspots();
void update_state_tree(HWND tree, const GameSnapshot& snapshot, std::wstring_view query = {});
void copy_state_item(HWND tree);
}
