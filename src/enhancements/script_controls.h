#pragma once

#include "game_ui.h"

namespace enhancements::game {
ScriptControls read_script_controls(void* state, std::byte* image, const Edition& profile);
}
