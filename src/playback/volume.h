#pragma once

#include <algorithm>
#include <cstdint>

namespace playback {

inline std::uint32_t stereo_volume(std::int16_t volume, std::int16_t balance) {
    const auto level = static_cast<std::uint32_t>(std::clamp<int>(volume, 0, 256) * 65535 / 256);
    const auto pan = std::clamp<int>(balance, -128, 127);
    const auto left = pan > 0 ? level * (127 - pan) / 127 : level;
    const auto right = pan < 0 ? level * (128 + pan) / 128 : level;
    return left | (right << 16);
}

}
