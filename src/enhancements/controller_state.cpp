#include "controller_state.h"

#include <windows.h>
#include <xinput.h>
#include <cmath>

namespace enhancements::input {
namespace {

bool meaningful(const Sample& sample) {
    return sample.buttons || sample.left_trigger > trigger_threshold ||
           sample.right_trigger > trigger_threshold ||
           std::abs(static_cast<int>(sample.left_x)) > left_deadzone ||
           std::abs(static_cast<int>(sample.left_y)) > left_deadzone;
}

class XInputBackend final : public Backend {
public:
    bool read(unsigned player, Sample& sample) override {
        XINPUT_STATE state{};
        if (XInputGetState(player, &state) != ERROR_SUCCESS) {
            return false;
        }
        const WORD raw = state.Gamepad.wButtons;
        const auto mapped = (raw & XINPUT_GAMEPAD_A ? button::activate : 0) |
                            (raw & XINPUT_GAMEPAD_B ? button::inventory : 0) |
                            (raw & XINPUT_GAMEPAD_X ? button::examine : 0) |
                            (raw & XINPUT_GAMEPAD_Y ? button::back : 0) |
                            (raw & XINPUT_GAMEPAD_BACK ? button::skip : 0) |
                            (raw & XINPUT_GAMEPAD_START ? button::menu : 0) |
                            (raw & XINPUT_GAMEPAD_LEFT_SHOULDER ? button::previous : 0) |
                            (raw & XINPUT_GAMEPAD_RIGHT_SHOULDER ? button::next : 0) |
                            (raw & XINPUT_GAMEPAD_RIGHT_THUMB ? button::evidence : 0) |
                            (raw & XINPUT_GAMEPAD_DPAD_UP ? button::up : 0) |
                            (raw & XINPUT_GAMEPAD_DPAD_DOWN ? button::down : 0) |
                            (raw & XINPUT_GAMEPAD_DPAD_LEFT ? button::left : 0) |
                            (raw & XINPUT_GAMEPAD_DPAD_RIGHT ? button::right : 0);
        sample = {static_cast<std::uint16_t>(mapped), state.Gamepad.bLeftTrigger,
                  state.Gamepad.bRightTrigger, state.Gamepad.sThumbLX, state.Gamepad.sThumbLY};
        return true;
    }
};

}

Frame Selection::poll(Backend& backend, bool enabled) {
    std::array<Sample, max_devices> samples{};
    std::array<bool, max_devices> connected{};
    unsigned candidate = no_device;
    Sample candidate_prior{};
    unsigned first = no_device;
    for (unsigned player = 0; player < max_devices; ++player) {
        connected[player] = backend.read(player, samples[player]);
        if (!connected[player]) {
            previous_[player] = {};
            seen_neutral_[player] = false;
            continue;
        }
        if (first == no_device) {
            first = player;
        }
        const bool activity = meaningful(samples[player]);
        const auto& previous = previous_[player];
        const bool new_activity =
            (samples[player].buttons & ~previous.buttons) ||
            (samples[player].left_trigger > trigger_threshold &&
             previous.left_trigger <= trigger_threshold) ||
            (samples[player].right_trigger > trigger_threshold &&
             previous.right_trigger <= trigger_threshold) ||
            (std::abs(static_cast<int>(samples[player].left_x)) > left_deadzone &&
             std::abs(static_cast<int>(previous.left_x)) <= left_deadzone) ||
            (std::abs(static_cast<int>(samples[player].left_y)) > left_deadzone &&
             std::abs(static_cast<int>(previous.left_y)) <= left_deadzone);
        if (!activity) {
            seen_neutral_[player] = true;
        } else if (enabled && seen_neutral_[player] && new_activity && candidate == no_device) {
            candidate = player;
            candidate_prior = previous;
        }
        previous_[player] = samples[player];
    }

    Frame frame{};
    bool lost_active = false;
    if (player_ != no_device && !connected[player_]) {
        player_ = no_device;
        frame.device_changed = true;
        lost_active = true;
    }
    const unsigned next = candidate != no_device ? candidate
                          : player_ != no_device ? player_
                                                 : first;
    if (next != player_) {
        player_ = next;
        frame.device_changed = true;
    }
    if (frame.device_changed || !enabled) {
        armed_ = enabled && !lost_active && candidate == player_;
        previous_buttons_ = 0;
        previous_aim_ = false;
        blocked_buttons_ = armed_ ? candidate_prior.buttons : 0;
        blocked_left_trigger_ = armed_ && candidate_prior.left_trigger > trigger_threshold;
        blocked_right_trigger_ = armed_ && candidate_prior.right_trigger > trigger_threshold;
        blocked_left_x_ =
            armed_ && std::abs(static_cast<int>(candidate_prior.left_x)) > left_deadzone;
        blocked_left_y_ =
            armed_ && std::abs(static_cast<int>(candidate_prior.left_y)) > left_deadzone;
    }
    if (player_ == no_device) {
        return frame;
    }
    frame.connected = true;
    frame.player = player_;
    const auto& sample = samples[player_];
    if (!enabled || !armed_) {
        if (enabled && !meaningful(sample)) {
            armed_ = true;
        }
        return frame;
    }
    auto effective = sample;
    blocked_buttons_ &= sample.buttons;
    effective.buttons &= ~blocked_buttons_;
    if (blocked_left_trigger_) {
        blocked_left_trigger_ = sample.left_trigger > trigger_threshold;
        effective.left_trigger = 0;
    }
    if (blocked_right_trigger_) {
        blocked_right_trigger_ = sample.right_trigger > trigger_threshold;
        effective.right_trigger = 0;
    }
    if (blocked_left_x_) {
        blocked_left_x_ = std::abs(static_cast<int>(sample.left_x)) > left_deadzone;
        effective.left_x = 0;
    }
    if (blocked_left_y_) {
        blocked_left_y_ = std::abs(static_cast<int>(sample.left_y)) > left_deadzone;
        effective.left_y = 0;
    }
    frame.sample = effective;
    frame.pressed = effective.buttons & ~previous_buttons_;
    previous_buttons_ = effective.buttons;
    const bool aim = effective.left_trigger > trigger_threshold;
    frame.aim_pressed = aim && !previous_aim_;
    previous_aim_ = aim;
    return frame;
}

Frame poll(bool enabled) {
    static XInputBackend backend;
    static thread_local Selection selection;
    return selection.poll(backend, enabled);
}

}
