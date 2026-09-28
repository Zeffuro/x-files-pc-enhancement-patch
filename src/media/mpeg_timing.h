#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}

namespace media::mpeg {

inline constexpr AVRational clock{1, 90000};

class Timeline {
public:
    void reset(std::optional<std::int64_t> start) {
        next_ = start;
        previous_.reset();
    }

    std::int64_t stamp(std::int64_t timestamp, AVRational time_base, std::int64_t origin,
                       std::int64_t duration) {
        if (duration <= 0) {
            throw std::runtime_error("MPEG frame has no duration");
        }
        const auto time = timestamp == AV_NOPTS_VALUE
                              ? next_.value_or(AV_NOPTS_VALUE)
                              : av_rescale_q(timestamp, time_base, clock) - origin;
        if (time == AV_NOPTS_VALUE || (previous_ && time < *previous_)) {
            throw std::runtime_error("MPEG timestamp is missing or moves backward");
        }
        if (time > INT64_MAX - duration) {
            throw std::runtime_error("MPEG timestamp overflow");
        }
        previous_ = time;
        next_ = time + duration;
        return time;
    }

private:
    std::optional<std::int64_t> next_;
    std::optional<std::int64_t> previous_;
};

}
