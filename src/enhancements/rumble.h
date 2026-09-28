#pragma once

#include <windows.h>
#include <cstdint>
#include "rumble_state.h"

namespace enhancements {

void attach_rumble(HWND window) noexcept;
void update_rumble(unsigned player, bool allowed) noexcept;
void play_rumble(rumble::Effect effect, std::uintptr_t source) noexcept;
void stop_rumble() noexcept;
void cancel_rumble(std::uintptr_t source) noexcept;
void detach_rumble() noexcept;

}
