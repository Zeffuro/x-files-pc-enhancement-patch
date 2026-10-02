#pragma once
#include "game_state.h"
#include <optional>
#include <array>
#include <map>
#include <tuple>

namespace devtools {
enum class StateVariableFilter { all, where_are_we, registered, state, hdb };

struct StateVariableView {
    std::vector<StateVariable> baseline;
    std::map<std::tuple<bool, std::uint32_t, std::uint32_t>, std::size_t> baseline_index;
    std::map<std::tuple<bool, std::uint32_t, std::uint32_t>, std::size_t> current_counts;
    std::vector<std::array<std::wstring, 7>> cells;
    std::vector<std::size_t> rows;
    std::vector<DatabaseObjectKey> keys;
    std::optional<DatabaseObjectKey> selected;
    std::vector<DatabaseObjectKey> watches;
    std::uintptr_t manager = 0, state = 0, hdb = 0, application = 0, session = 0;
    int sort_column = 0;
    bool descending = false;
};

struct StateVariableEdit {
    std::optional<StateVariable> variable;
    std::array<std::uintptr_t, 7> context{};
};

void update_state_edit_value(HWND edit, StateVariableEdit& draft, const GameSnapshot& snapshot,
                             const StateVariable* variable);
std::wstring state_variable_text(const StateVariable& variable);
std::wstring state_variable_type(const StateVariable& variable);
void reset_state_baseline(StateVariableView& view, const GameSnapshot& snapshot);
void update_state_variables(HWND list, StateVariableView& view, const GameSnapshot& snapshot,
                            std::wstring_view query, StateVariableFilter filter, bool nonzero,
                            bool changed, bool watched = false);
const StateVariable* selected_state_variable(HWND list, const StateVariableView& view,
                                             const GameSnapshot& snapshot);
bool is_state_watched(const StateVariableView& view, const DatabaseObjectKey& key);
void toggle_state_watch(StateVariableView& view, const DatabaseObjectKey& key);
void copy_state_variable(HWND list, const StateVariableView& view, const GameSnapshot& snapshot);
}
