#include "ima4.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>

namespace media {
namespace {

constexpr std::array<int, 89> steps = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
constexpr int index_change[] = {-1, -1, -1, -1, 2, 4, 6, 8};
constexpr unsigned index_mask = 0x7f;
constexpr unsigned negative = 8;

}

Ima4::Ima4(unsigned channels) : channels_(channels) {
    if (channels < 1 || channels > state_.size()) {
        throw std::runtime_error("IMA4 requires mono or stereo audio");
    }
}

std::vector<std::int16_t> Ima4::decode(std::span<const std::uint8_t> packet) {
    if (packet.size() != packet_bytes * channels_) {
        throw std::runtime_error("Invalid IMA4 packet size");
    }
    auto next = state_;
    std::vector<std::int16_t> pcm(packet_frames * channels_);
    for (unsigned channel = 0; channel < channels_; ++channel) {
        auto& state = next[channel];
        const auto block = packet.subspan(channel * packet_bytes, packet_bytes);
        const unsigned header = (block[0] << 8) | block[1];
        const auto index = static_cast<int>(header & index_mask);
        const auto predictor = static_cast<std::int16_t>(header & ~index_mask);
        if (index >= std::size(steps)) {
            throw std::runtime_error("Invalid IMA4 step index");
        }
        // Packet headers omit seven predictor bits; retain them across continuous packets.
        if (index != state.index || std::abs(predictor - state.predictor) > index_mask) {
            state = {predictor, index};
        }
        for (unsigned frame = 0; frame < packet_frames; ++frame) {
            const unsigned code = (block[2 + frame / 2] >> ((frame % 2) * 4)) & 15;
            const auto step = steps.at(state.index);
            int delta = step >> 3;
            if (code & 4) {
                delta += step;
            }
            if (code & 2) {
                delta += step >> 1;
            }
            if (code & 1) {
                delta += step >> 2;
            }
            state.predictor =
                std::clamp(state.predictor + ((code & negative) ? -delta : delta), -32768, 32767);
            state.index = std::clamp(state.index + index_change[code & 7], 0, 88);
            pcm[frame * channels_ + channel] = static_cast<std::int16_t>(state.predictor);
        }
    }
    state_ = next;
    return pcm;
}

}
