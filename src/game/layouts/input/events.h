#pragma once
#include "game/layouts/ui.h"
#include <array>

namespace native_game {
inline constexpr std::array<const wchar_t*, 12> input_event_names{
    L"Click",      L"Double-click",    L"Mouse move",
    L"Key press",  L"Pointer enter",   L"Pointer leave",
    L"Drop",       L"Timer",           L"Activate",
    L"Deactivate", L"Window activate", L"Window deactivate"};

struct InputEvents {
    std::byte prefix[0x1c];
    std::array<List<void>, 12> actions;
};

static_assert(sizeof(List<void>) == 0x14);
static_assert(offsetof(InputEvents, actions) == 0x1c);
inline constexpr unsigned pointer_event_mask = 0x77;
}
