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

    void play(std::uint32_t time, std::uint32_t scale, std::int16_t volume, unsigned speed = 1);
    void prepare(unsigned speed);
    void stop();
    void volume(std::int16_t value);
    void balance(std::int16_t value);
    void refresh(std::uint32_t time, std::uint32_t scale);

private:
    std::vector<std::int16_t> pcm_;
    std::vector<std::int16_t> fast_pcm_;
    unsigned prepared_speed_ = 0;
    WAVEFORMATEX format_{};
    std::unique_ptr<Output> output_;
    std::int16_t volume_ = 256;
    std::int16_t balance_ = 0;
    unsigned speed_ = 1;
};

}
