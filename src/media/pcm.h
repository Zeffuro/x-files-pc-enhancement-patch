#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace media {

inline std::vector<std::int16_t> decode_signed_pcm(std::span<const std::uint8_t> bytes,
                                                   unsigned depth) {
    if ((depth != 8 && depth != 16) || bytes.size() % (depth / 8)) {
        throw std::runtime_error("Invalid signed PCM sample format");
    }
    std::vector<std::int16_t> result;
    result.reserve(bytes.size() / (depth / 8));
    for (std::size_t offset = 0; offset < bytes.size(); offset += depth / 8) {
        if (depth == 8) {
            const auto sample = bytes[offset] < 128 ? int(bytes[offset]) : int(bytes[offset]) - 256;
            result.push_back(static_cast<std::int16_t>(sample * 256));
        } else {
            result.push_back(static_cast<std::int16_t>((bytes[offset] << 8) | bytes[offset + 1]));
        }
    }
    return result;
}

}
