#pragma once

#include "game_ui.h"

namespace enhancements::game {
Interaction world_interaction(const void* object, const void* resource, Application* app,
                              std::byte* image, const Edition& profile);
}
