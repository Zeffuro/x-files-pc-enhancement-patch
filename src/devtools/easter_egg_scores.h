#pragma once
#include "game/profiles/generated.h"
#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace devtools {
struct EasterEggScore {
    std::wstring_view label;
    std::uint32_t id;
    std::wstring name;
    std::optional<std::int32_t> raw_value;
};

struct EasterEggScores {
    std::array<EasterEggScore, 6> scores;
    bool available = false;
};

EasterEggScores capture_easter_egg_scores(const std::byte* image,
                                          const native_game::Profile& profile);
}
