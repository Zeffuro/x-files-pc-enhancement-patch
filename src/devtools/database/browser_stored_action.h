#pragma once
#include "browser_state.h"

namespace devtools::database_browser {
void stored_action_row(const Browser& state, const game_assets::StoredDatabaseRecord& record,
                       Row& row);
void stored_action_properties(Browser& state, const game_assets::StoredDatabaseRecord& record,
                              std::wstring& raw);
}
