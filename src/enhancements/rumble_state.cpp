#include "rumble_state.h"

#include <algorithm>

namespace enhancements::rumble {

void State::silence(Output& output) noexcept {
    playing_ = false;
    end_ = 0;
    source_ = 0;
    requested_ = {};
    motors_ = {};
    for (unsigned player = 0; player < device_count; ++player) {
        if (pending_stop_[player] && output.set(player, {})) {
            pending_stop_[player] = false;
        }
    }
}

void State::stop(Output& output) noexcept {
    silence(output);
    player_ = no_device;
    lease_ = 0;
}

void State::cancel(Output& output) noexcept {
    silence(output);
}

void State::cancel(Output& output, std::uintptr_t source) noexcept {
    if (source == source_) {
        silence(output);
    }
}

void State::update(Output& output, unsigned player, bool allowed, std::uint64_t now) noexcept {
    if (!allowed || player >= device_count) {
        stop(output);
        return;
    }
    if (player != player_ || now >= lease_) {
        stop(output);
        player_ = player;
    }
    // A stalled input loop must not leave an event eligible indefinitely.
    lease_ = now + 100;
    tick(output, now);
}

void State::play(Output& output, Effect effect, std::uintptr_t source, std::uint64_t now) noexcept {
    if (player_ >= device_count || now >= lease_) {
        stop(output);
        return;
    }
    if (!source || !effect.duration_ms || effect.duration_ms > 2000 || effect.motors == Motors{}) {
        return;
    }
    end_ = std::max(end_, now + effect.duration_ms);
    requested_ = effect.motors;
    source_ = source;
    playing_ = true;
    tick(output, now);
}

void State::tick(Output& output, std::uint64_t now) noexcept {
    if (now >= lease_) {
        stop(output);
        return;
    }
    for (unsigned player = 0; player < device_count; ++player) {
        if (player != player_ && pending_stop_[player] && output.set(player, {})) {
            pending_stop_[player] = false;
        }
    }
    if (!playing_) {
        silence(output);
        return;
    }
    if (now >= end_) {
        silence(output);
        return;
    }
    const Motors next = requested_;
    if (next != motors_) {
        pending_stop_[player_] = true;
        if (!output.set(player_, next)) {
            stop(output);
            return;
        }
        motors_ = next;
    }
}

}
