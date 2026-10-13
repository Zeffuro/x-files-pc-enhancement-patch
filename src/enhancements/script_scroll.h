#pragma once

#include "game_ui.h"
#include "game/calls.h"

namespace enhancements::game {
using native_game::ProcessCommands;
using native_game::ScrollPhotos;
using native_game::ScrollText;

bool scroll_script_button(void* state, void* view, std::byte* image, const Edition& profile,
                          unsigned resource, const RECT& bounds, ScrollText up = nullptr,
                          ScrollText down = nullptr);
ScrollResult scroll_photo_button(void* state, void* view, std::byte* image, const Edition& profile,
                                 const RECT& bounds, ScrollPhotos up = nullptr,
                                 ScrollPhotos down = nullptr, ProcessCommands flush = nullptr);
}
