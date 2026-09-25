#pragma once

#include "movie.h"

#include <memory>

namespace media {

class CompressedAudio {
public:
    explicit CompressedAudio(const Description& description);
    ~CompressedAudio();
    CompressedAudio(const CompressedAudio&) = delete;
    CompressedAudio& operator=(const CompressedAudio&) = delete;

    std::vector<std::int16_t> decode(std::span<const std::uint8_t> packet);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
