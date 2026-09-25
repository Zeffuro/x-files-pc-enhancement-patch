#pragma once

#include "media/movie.h"
#include "output.h"

#include <windows.h>
#include <mmsystem.h>

namespace playback {

class Audio {
public:
    Audio(const media::Movie& movie, const media::Track& track);
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    void play(std::uint32_t time, std::uint32_t scale, std::int16_t volume);
    void stop();
    void volume(std::int16_t value);
    void balance(std::int16_t value);
    void refresh(std::uint32_t time, std::uint32_t scale);

private:
    std::vector<std::int16_t> pcm_;
    WAVEFORMATEX format_{};
    std::unique_ptr<Output> output_;
    std::int16_t volume_ = 256;
    std::int16_t balance_ = 0;
};

}
