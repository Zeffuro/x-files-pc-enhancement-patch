#pragma once
#include "game/profiles/generated.h"
#include <string>
#include <cstddef>

namespace devtools {
std::wstring inspect_variables(const std::byte* image, const native_game::Profile& profile);
}
