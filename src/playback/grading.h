#pragma once

#include "settings.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace playback {
struct Grade {
    int contrast = 0;
    int brightness = 0;
    int gamma = 100;
    int black = 0;
    int white = 255;
    int red = 100;
    int green = 100;
    int blue = 100;
    bool operator==(const Grade&) const = default;
};

Grade scene_grade(std::string_view path);
Grade movie_grade(MovieContrast mode, std::string_view path, std::string_view codec,
                  std::size_t samples);
void apply_grade(std::span<std::uint8_t> pixels, const Grade& grade);
}
