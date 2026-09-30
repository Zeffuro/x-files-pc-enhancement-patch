#pragma once

#include "controller_profile.h"

#include <array>

namespace playback {
struct FastForwardInput {
    bool armed = false;
    unsigned context_epoch = 0;
    unsigned epoch = 0;
    unsigned generation = 0;
    std::array<bool, 4> controllers{};
    controller::Profile profile;
};

inline thread_local FastForwardInput movie_speed_input;

class HeldFastForward {
public:
    HeldFastForward() = default;

    explicit HeldFastForward(FastForwardInput& input) : shared_(&input) {}

    bool update(bool down, bool allowed) {
        if (!allowed) {
            reset();
        } else if (!down) {
            input().armed = true;
            active_ = false;
        } else {
            active_ = input().armed;
        }
        generation_ = input().generation;
        return active();
    }

    void reset() {
        input().armed = false;
        input().controllers.fill(false);
        ++input().generation;
        clear();
    }

    void clear() {
        active_ = false;
    }

    bool controller(unsigned index, bool connected, bool down) {
        auto& armed = input().controllers.at(index);
        if (!connected) {
            armed = false;
        } else if (!down) {
            armed = true;
        }
        return connected && down && armed;
    }

    bool active() const {
        return active_ && generation_ == input().generation;
    }

    bool previously_active() const {
        // A shared reset still needs the caller's accelerated audio to be rebased.
        return active_;
    }

    void context() {
        context(input().context_epoch);
    }

    void context(unsigned epoch) {
        if (epoch != input().epoch) {
            reset();
            input().epoch = epoch;
        }
    }

    void profile(const controller::Profile& requested) {
        const auto profile = controller::normalize(requested);
        if (profile != input().profile) {
            reset();
            input().profile = profile;
        }
    }

private:
    FastForwardInput& input() {
        return shared_ ? *shared_ : local_;
    }

    const FastForwardInput& input() const {
        return shared_ ? *shared_ : local_;
    }

    FastForwardInput local_;
    FastForwardInput* shared_ = nullptr;
    bool active_ = false;
    unsigned generation_ = 0;
};
}
