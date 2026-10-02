#pragma once
#include "browser_state.h"

namespace devtools::database_browser {
void stored_fields_row(const Browser& state, const game_assets::StoredDatabaseRecord& record,
                       Row& row);
void stored_fields_properties(Browser& state, const game_assets::StoredDatabaseRecord& record,
                              std::wstring& raw);
}
