#pragma once

#include <cstdint>
#include <algorithm>
#include <span>
#include <string_view>
#include <vector>
#include <cstdlib>

namespace playback {

inline bool is_menu_animation(std::string_view filename) {
    return filename == "49587.xmv" || filename == "49587.XMV";
}

inline bool is_menu_entrance(std::string_view filename) {
    return is_menu_animation(filename) || filename == "49583.xmv" || filename == "49583.XMV" ||
           filename == "49589.xmv" || filename == "49589.XMV";
}

inline bool is_credit_pages(std::string_view path, std::string_view codec, unsigned width,
                            unsigned height, std::size_t samples) {
    constexpr std::string_view expected = "xv/19666.xmv";
    return codec == "jpeg" && width == 600 && height == 400 && (samples == 18 || samples == 19) &&
           std::ranges::equal(path, expected, [](char actual, char wanted) {
               if (actual >= 'A' && actual <= 'Z') {
                   actual = static_cast<char>(actual + ('a' - 'A'));
               }
               return (actual == '\\' ? '/' : actual) == wanted;
           });
}

inline void clean_credit_colors(std::span<std::uint8_t> pixels) {
    // These text-only JPEG pages have colored ringing around otherwise sharp letters.
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4) {
        if (std::max({pixels[offset], pixels[offset + 1], pixels[offset + 2]}) <= 24) {
            pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 0;
        }
    }
}

inline void restore_menu_black(std::span<std::uint8_t> pixels) {
    // JPEG ringing includes slightly tinted near-black pixels. Keep the blue glow intact.
    constexpr std::uint8_t darkest_gray = 16;
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4) {
        const auto low = std::min({pixels[offset], pixels[offset + 1], pixels[offset + 2]});
        const auto high = std::max({pixels[offset], pixels[offset + 1], pixels[offset + 2]});
        if (high <= darkest_gray && high - low <= 2) {
            pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 0;
        }
    }
}

inline void clean_menu_colors(std::span<std::uint8_t> pixels, unsigned width, unsigned height) {
    if (!width || !height || pixels.size() / 4 / width != height || pixels.size() % 4 ||
        pixels.size() / 4 % width) {
        return;
    }
    const std::vector<std::uint8_t> source(pixels.begin(), pixels.end());
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto offset = (std::size_t(y) * width + x) * 4;
            unsigned sum[3]{};
            unsigned total = 0;
            // A small edge-aware kernel smooths ringing without merging the letter strokes.
            for (int dy = -1; dy <= 1; ++dy) {
                const auto row = dy < 0 ? (y ? y - 1 : 0) : std::min(y + unsigned(dy), height - 1);
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto column =
                        dx < 0 ? (x ? x - 1 : 0) : std::min(x + unsigned(dx), width - 1);
                    const auto neighbor = (std::size_t(row) * width + column) * 4;
                    int difference = 0;
                    for (unsigned c = 0; c < 3; ++c) {
                        difference = std::max(difference, std::abs(int(source[neighbor + c]) -
                                                                   int(source[offset + c])));
                    }
                    const auto weight =
                        unsigned((dx ? 1 : 2) * (dy ? 1 : 2) * std::max(0, 24 - difference));
                    for (unsigned c = 0; c < 3; ++c) {
                        sum[c] += weight * source[neighbor + c];
                    }
                    total += weight;
                }
            }
            unsigned high = 0;
            for (unsigned c = 0; c < 3; ++c) {
                sum[c] = (sum[c] + total / 2) / total;
                high = std::max(high, sum[c]);
            }
            // Fade weak colored noise into black instead of cutting a hard edge into the glow.
            for (unsigned c = 0; c < 3; ++c) {
                pixels[offset + c] =
                    static_cast<std::uint8_t>(high <= 3   ? 0
                                              : high < 20 ? (sum[c] * (high - 3) + 8) / 17
                                                          : sum[c]);
            }
        }
    }
    restore_menu_black(pixels);
}

}
