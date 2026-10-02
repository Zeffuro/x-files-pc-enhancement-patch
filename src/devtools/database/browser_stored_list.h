#pragma once
#include "browser_state.h"

namespace devtools::database_browser {
void stored_list_properties(Browser& state, const game_assets::StoredDatabaseRecord& record);
void stored_list_row(const Browser& state, const game_assets::StoredDatabaseRecord& record,
                     Row& row);
}
