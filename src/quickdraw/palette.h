#pragma once

#include "types.h"

namespace quickdraw {

inline ColorTable default_palette() {
    ColorTable table{1, 0x8000, 255, {}};
    std::int16_t index = 0;
    for (int red = 5; red >= 0; --red) {
        for (int green = 5; green >= 0; --green) {
            for (int blue = 5; blue >= 0; --blue) {
                table.colors[index] = {index,
                                       {static_cast<std::uint16_t>(red * 13107),
                                        static_cast<std::uint16_t>(green * 13107),
                                        static_cast<std::uint16_t>(blue * 13107)}};
                ++index;
            }
        }
    }
    --index;
    for (int channel = 0; channel < 4; ++channel) {
        for (int step = 14; step > 0; --step) {
            if (step % 3 == 0) {
                continue;
            }
            const auto level = static_cast<std::uint16_t>(step * 4369);
            Color color{};
            if (channel == 0 || channel == 3) {
                color.red = level;
            }
            if (channel == 1 || channel == 3) {
                color.green = level;
            }
            if (channel == 2 || channel == 3) {
                color.blue = level;
            }
            table.colors[index] = {index, color};
            ++index;
        }
    }
    table.colors[255] = {255, {0, 0, 0}};
    return table;
}

}
