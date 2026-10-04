#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace media {

class Tempo {
public:
    Tempo(unsigned sample_rate, unsigned channels, unsigned speed = 2);
    ~Tempo();
    Tempo(const Tempo&) = delete;
    Tempo& operator=(const Tempo&) = delete;

    std::vector<std::int16_t> push(std::span<const std::int16_t> samples);
    // The cumulative output contains exactly floor(input frames / speed) frames.
    std::vector<std::int16_t> finish();

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
