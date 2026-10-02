#pragma once
#include "native.h"

namespace devtools {
void inspect_database_fields(NativeDatabaseObject& object, const std::byte* image,
                             const native_game::Profile& profile);
}
