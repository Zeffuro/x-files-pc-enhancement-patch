#pragma once

#include "movie.h"

#include <memory>

namespace media {

struct Frame {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<std::uint8_t> pixels;
};

class Video {
public:
    Video();
    ~Video();
    Video(const Video&) = delete;
    Video& operator=(const Video&) = delete;

    const Frame& image(const Description& format, std::span<const std::uint8_t> packet);
    const Frame& decode(const Movie& movie, const Track& track, std::size_t sample);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
