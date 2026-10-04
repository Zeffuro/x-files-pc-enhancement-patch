#pragma once

#include "game_ui.h"

namespace enhancements::game {
using ScrollText = void(__stdcall*)(ChoiceList*);

bool scroll_script_button(void* state, void* view, std::byte* image, const Edition& profile,
                          unsigned resource, const RECT& bounds, ScrollText up = nullptr,
                          ScrollText down = nullptr);
}
