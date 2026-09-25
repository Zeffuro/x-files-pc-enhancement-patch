#pragma once

#include <cstdint>

namespace playback {

enum class MediaSelector : std::int16_t {
    SetSoundBalance = 0x0514,
    GetSoundBalance = 0x0515,
};

struct ComponentParameters {
    std::uint8_t flags;
    std::uint8_t size;
    MediaSelector selector;
    std::uint32_t argument;
};

static_assert(sizeof(ComponentParameters) == 8);

std::int32_t __cdecl call_component(void* component, const ComponentParameters* parameters);

}
