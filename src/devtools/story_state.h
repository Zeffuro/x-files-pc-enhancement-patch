#pragma once
#include "database/native.h"
#include "game_state.h"

namespace devtools {
// Call on the game thread immediately after the cache capture.
std::vector<StateGroup> story_state_groups(const NativeDatabaseSnapshot& snapshot,
                                           bool include_values = true);
std::vector<StateVariable> story_state_variables(const NativeDatabaseSnapshot& snapshot,
                                                 const std::byte* image = nullptr,
                                                 const native_game::Profile* profile = nullptr);
std::wstring story_variable_name(const NativeDatabaseObject& object);
}
