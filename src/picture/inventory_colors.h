#pragma once

#include "inventory_art.h"
#include <vector>

namespace picture {

inline void clean_inventory_background(std::span<std::uint8_t> pixels, unsigned width,
                                       unsigned height) {
    if (!width || !height || pixels.size() / 4 / width != height || pixels.size() % 4 ||
        pixels.size() / 4 % width) {
        return;
    }
    std::vector<bool> visited(pixels.size() / 4);
    std::vector<std::size_t> pending;
    const auto visit = [&](std::size_t index) {
        if (visited[index]) {
            return;
        }
        visited[index] = true;
        const auto offset = index * 4;
        const auto low = std::min({pixels[offset], pixels[offset + 1], pixels[offset + 2]});
        const auto high = std::max({pixels[offset], pixels[offset + 1], pixels[offset + 2]});
        if (high <= 16 && high - low <= 6) {
            pending.push_back(index);
        }
    };
    for (unsigned x = 0; x < width; ++x) {
        visit(x);
        visit(std::size_t(height - 1) * width + x);
    }
    for (unsigned y = 0; y < height; ++y) {
        visit(std::size_t(y) * width);
        visit(std::size_t(y) * width + width - 1);
    }
    // Only edge-connected background is keyed. Enclosed shadows keep their original colors.
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        const auto offset = index * 4;
        pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 0;
        const auto x = index % width;
        const auto y = index / width;
        if (x) {
            visit(index - 1);
        }
        if (x + 1 < width) {
            visit(index + 1);
        }
        if (y) {
            visit(index - width);
        }
        if (y + 1 < height) {
            visit(index + width);
        }
    }
}

inline std::vector<std::uint8_t> inventory_colors(std::span<const std::uint8_t> pixels,
                                                  std::span<const std::uint8_t> packet,
                                                  unsigned width, unsigned height) {
    if (!is_inventory_art(packet, width, height)) {
        return {};
    }
    std::vector<std::uint8_t> result(pixels.begin(), pixels.end());
    clean_inventory_background(result, width, height);
    return result;
}

}
