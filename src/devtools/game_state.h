#pragma once
#include "database/native.h"
#include <windows.h>
#include <string>
#include <string_view>
#include <vector>

namespace devtools {
struct StateGroup {
    std::wstring name;
    std::vector<std::wstring> values;
};

struct StateVariable {
    DatabaseObjectKey key;
    std::wstring name;
    std::wstring registration;
    std::optional<DatabaseVariable> value;
    std::uintptr_t address = 0;
    std::vector<std::uint8_t> bytes;
};

struct GameSnapshot {
    std::wstring text;
    std::vector<RECT> targets;
    std::vector<StateGroup> groups;
    std::vector<StateVariable> variables;
    std::uintptr_t manager = 0, state = 0, hdb = 0, view = 0, input = 0, application = 0,
                   session = 0;
};

GameSnapshot inspect_game(bool include_story = true);
std::vector<RECT> collect_interaction_targets();
std::wstring edit_game_variable(const GameSnapshot& snapshot, const StateVariable& variable,
                                std::wstring_view value, bool enabled);
void show_hotspots(HWND game, bool enabled, const std::vector<RECT>& targets);
void release_hotspots();
void update_state_tree(HWND tree, const GameSnapshot& snapshot, std::wstring_view query = {});
void copy_state_item(HWND tree);
}
