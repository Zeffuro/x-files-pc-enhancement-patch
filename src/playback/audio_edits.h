#pragma once

#include "media/movie.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace playback {
inline std::vector<std::int16_t> edit_audio(std::span<const std::int16_t> decoded,
                                            const media::Track& track, unsigned movie_scale) {
    constexpr std::uint64_t limit = 64 * 1024 * 1024;
    const auto channels = track.descriptions.at(0).channels;
    const auto rate = track.descriptions.at(0).sample_rate;
    if (!channels || !rate || !movie_scale || decoded.size() % channels) {
        throw std::runtime_error("Invalid audio edit format");
    }
    std::vector<std::int16_t> output;
    std::uint64_t end_time = 0;
    for (const auto& edit : track.edits) {
        if (edit.duration > std::numeric_limits<std::uint64_t>::max() - end_time) {
            throw std::runtime_error("Audio edit duration overflow");
        }
        end_time += edit.duration;
        if (end_time > std::numeric_limits<std::uint64_t>::max() / rate) {
            throw std::runtime_error("Audio edit duration overflow");
        }
        const auto frames = end_time * rate / movie_scale;
        if (frames > limit / channels || edit.rate < 0 || edit.media_time < -1) {
            throw std::runtime_error("Invalid audio edit range");
        }
        const auto start = output.size();
        output.resize(static_cast<std::size_t>(frames * channels));
        if (edit.media_time == -1 || edit.rate == 0 || start == output.size()) {
            continue;
        }
        const auto source_frames = decoded.size() / channels;
        const auto count = (output.size() - start) / channels;
        if (static_cast<std::uint64_t>(edit.media_time) >= source_frames) {
            throw std::runtime_error("Audio edit exceeds decoded samples");
        }
        const auto origin = static_cast<std::uint64_t>(edit.media_time) << 16;
        const auto last = origin + (count - 1) * static_cast<std::uint64_t>(edit.rate);
        if ((last >> 16) >= source_frames) {
            throw std::runtime_error("Audio edit exceeds decoded samples");
        }
        // Edit rates are 16.16 fixed point; preserve channel alignment while resampling.
        for (std::size_t frame = 0; frame < count; ++frame) {
            const auto position = origin + frame * static_cast<std::uint64_t>(edit.rate);
            const auto first = static_cast<std::size_t>(position >> 16);
            const auto next = std::min(first + 1, source_frames - 1);
            const auto fraction = static_cast<std::int64_t>(position & 0xffff);
            for (unsigned channel = 0; channel < channels; ++channel) {
                const auto a = decoded[first * channels + channel];
                const auto b = decoded[next * channels + channel];
                output[start + frame * channels + channel] =
                    static_cast<std::int16_t>(a + (b - a) * fraction / 65536);
            }
        }
    }
    return output;
}
}
