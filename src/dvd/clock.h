#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace dvd {
class Clock {
public:
    void reset(std::int64_t time, std::uint64_t now) {
        if (time < 0) {
            throw std::runtime_error("Invalid DVD clock origin");
        }
        origin_ = position_ = time;
        anchor(now);
    }

    void anchor(std::uint64_t now) {
        last_ = now;
    }

    std::int64_t advance(std::uint64_t now, std::int64_t played, unsigned speed, bool muted,
                         bool tail) {
        if (now < last_ || played < 0 || played > INT64_MAX - origin_ || speed < 1 || speed > 4) {
            throw std::runtime_error("Invalid DVD audio clock");
        }
        if (muted || tail) {
            const auto rate = speed * 90u;
            const auto elapsed = now - last_;
            if (elapsed > static_cast<std::uint64_t>(INT64_MAX - position_) / rate) {
                throw std::runtime_error("DVD presentation clock overflow");
            }
            position_ += static_cast<std::int64_t>(elapsed * rate);
        }
        if (!muted) {
            position_ = std::max(position_, origin_ + played);
        }
        last_ = now;
        return position_;
    }

    std::int64_t position() const {
        return position_;
    }

private:
    std::int64_t origin_ = 0;
    std::int64_t position_ = 0;
    std::uint64_t last_ = 0;
};
}
