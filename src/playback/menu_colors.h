#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace playback {

inline bool is_menu_animation(std::string_view filename) {
    return filename == "49587.xmv" || filename == "49587.XMV";
}

inline bool is_menu_entrance(std::string_view filename) {
    return is_menu_animation(filename) || filename == "49583.xmv" || filename == "49583.XMV" ||
           filename == "49589.xmv" || filename == "49589.XMV";
}

inline void restore_menu_black(std::span<std::uint8_t> pixels) {
    // Remove the JPEG's raised black floor and neutral ringing around the blue text.
    constexpr std::uint8_t darkest_gray = 7;
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4) {
        if (pixels[offset] <= darkest_gray && pixels[offset] == pixels[offset + 1] &&
            pixels[offset] == pixels[offset + 2]) {
            pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 0;
        }
    }
}

}
