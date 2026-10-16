#pragma once
#include "game/profiles/generated.h"
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace native_story {
std::string quote(std::string_view value);
std::string snapshot(HANDLE process, std::uintptr_t image, const native_game::Build& build);
}
