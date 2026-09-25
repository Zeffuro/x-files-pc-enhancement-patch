#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace media {

class Ima4 {
public:
    static constexpr unsigned packet_bytes = 34;
    static constexpr unsigned packet_frames = 64;

    explicit Ima4(unsigned channels);
    std::vector<std::int16_t> decode(std::span<const std::uint8_t> packet);

private:
    struct Channel {
        int predictor = 0;
        int index = 0;
    };

    unsigned channels_;
    std::array<Channel, 2> state_{};
};

}
