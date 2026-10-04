#pragma once
#include "game_ui.h"

namespace enhancements::game {
Interaction world_cursor_interaction(const void* object, const void* resource, Application* app,
                                     std::byte* image, const Edition& profile,
                                     Interaction fallback);
Interaction navigation_cursor_interaction(const void* object, const void* wrapper,
                                          const void* shape, std::byte* image,
                                          const Edition& profile);
}
