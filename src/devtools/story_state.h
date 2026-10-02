#pragma once
#include "database/native.h"
#include "game_state.h"

namespace devtools {
// Call on the game thread immediately after the cache capture.
std::vector<StateGroup> story_state_groups(const NativeDatabaseSnapshot& snapshot);
}
