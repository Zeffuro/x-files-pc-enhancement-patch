#include "controller_state.h"

#include <windows.h>
#include <xinput.h>
#include <cmath>

namespace enhancements::input {
namespace {

bool meaningful(const Sample& sample, const controller::Profile& profile) {
    return sample.buttons || sample.left_trigger > trigger_threshold ||
           sample.right_trigger > trigger_threshold ||
           std::abs(static_cast<int>(sample.left_x)) > static_cast<int>(profile.deadzone) ||
           std::abs(static_cast<int>(sample.left_y)) > static_cast<int>(profile.deadzone);
}

class XInputBackend final : public Backend {
public:
    controller::Profile profile;

    bool read(unsigned player, Sample& sample) override {
        RawState raw;
        if (!read_raw(player, raw)) {
            return false;
        }
        sample = map_sample(raw, profile);
        return true;
    }
};

XInputBackend& runtime_backend() {
    static thread_local XInputBackend backend;
    return backend;
}

}

bool read_raw(unsigned player, RawState& raw) {
    raw = {};
    XINPUT_STATE state{};
    if (player >= max_devices || XInputGetState(player, &state) != ERROR_SUCCESS) {
        return false;
    }
    raw = {state.Gamepad.wButtons, state.Gamepad.bLeftTrigger, state.Gamepad.bRightTrigger,
           state.Gamepad.sThumbLX, state.Gamepad.sThumbLY};
    return true;
}

Sample map_sample(const RawState& raw, const controller::Profile& requested) {
    const auto profile = controller::normalize(requested);
    constexpr std::array<std::uint16_t, 10> actions{
        button::activate, button::inventory, button::examine, button::back,     button::skip,
        button::menu,     button::previous,  button::next,    button::evidence, button::speed};
    Sample sample;
    const auto held = [&](controller::Binding binding) {
        return controller::held(binding, raw.buttons, raw.left_trigger, raw.right_trigger,
                                profile.trigger_threshold);
    };
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (held(profile.bindings[index])) {
            sample.buttons |= actions[index];
        }
    }
    sample.buttons |= (raw.buttons & XINPUT_GAMEPAD_DPAD_UP ? button::up : 0) |
                      (raw.buttons & XINPUT_GAMEPAD_DPAD_DOWN ? button::down : 0) |
                      (raw.buttons & XINPUT_GAMEPAD_DPAD_LEFT ? button::left : 0) |
                      (raw.buttons & XINPUT_GAMEPAD_DPAD_RIGHT ? button::right : 0);
    const auto trigger = [&](controller::Action action) -> std::uint8_t {
        const auto binding = profile.bindings[static_cast<std::size_t>(action)];
        if (profile.trigger_threshold == trigger_threshold) {
            if (binding == controller::Binding::LeftTrigger) {
                return raw.left_trigger;
            }
            if (binding == controller::Binding::RightTrigger) {
                return raw.right_trigger;
            }
        }
        if (!held(binding)) {
            return 0;
        }
        if (binding == controller::Binding::LeftTrigger) {
            return static_cast<std::uint8_t>(
                std::max<unsigned>(trigger_threshold + 1, raw.left_trigger));
        }
        if (binding == controller::Binding::RightTrigger) {
            return static_cast<std::uint8_t>(
                std::max<unsigned>(trigger_threshold + 1, raw.right_trigger));
        }
        return 255;
    };
    sample.left_trigger = trigger(controller::Action::Aim);
    sample.right_trigger = trigger(controller::Action::Targets);
    const auto axis = [](std::int16_t value, bool inverted) {
        return static_cast<std::int16_t>(inverted ? std::min(32767, -static_cast<int>(value))
                                                  : value);
    };
    sample.left_x = axis(raw.left_x, profile.invert_x);
    sample.left_y = axis(raw.left_y, profile.invert_y);
    return sample;
}

Frame Selection::poll(Backend& backend, bool enabled, const controller::Profile& requested) {
    const auto profile = controller::normalize(requested);
    const bool profile_changed = profile != profile_;
    if (profile_changed) {
        profile_ = profile;
        previous_ = {};
        seen_neutral_ = {};
        armed_ = false;
    }
    const auto stick_deadzone = static_cast<int>(profile.deadzone);
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
        const bool activity = meaningful(samples[player], profile);
        const auto& previous = previous_[player];
        const bool new_activity =
            (samples[player].buttons & ~previous.buttons) ||
            (samples[player].left_trigger > trigger_threshold &&
             previous.left_trigger <= trigger_threshold) ||
            (samples[player].right_trigger > trigger_threshold &&
             previous.right_trigger <= trigger_threshold) ||
            (std::abs(static_cast<int>(samples[player].left_x)) > stick_deadzone &&
             std::abs(static_cast<int>(previous.left_x)) <= stick_deadzone) ||
            (std::abs(static_cast<int>(samples[player].left_y)) > stick_deadzone &&
             std::abs(static_cast<int>(previous.left_y)) <= stick_deadzone);
        if (!activity) {
            seen_neutral_[player] = true;
        } else if (enabled && !profile_changed && seen_neutral_[player] && new_activity &&
                   candidate == no_device) {
            candidate = player;
            candidate_prior = previous;
        }
        previous_[player] = samples[player];
    }

    Frame frame{};
    frame.device_changed = profile_changed;
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
        armed_ = enabled && !profile_changed && !lost_active && candidate == player_;
        previous_buttons_ = 0;
        previous_aim_ = false;
        blocked_buttons_ = armed_ ? candidate_prior.buttons : 0;
        blocked_left_trigger_ = armed_ && candidate_prior.left_trigger > trigger_threshold;
        blocked_right_trigger_ = armed_ && candidate_prior.right_trigger > trigger_threshold;
        blocked_left_x_ =
            armed_ && std::abs(static_cast<int>(candidate_prior.left_x)) > stick_deadzone;
        blocked_left_y_ =
            armed_ && std::abs(static_cast<int>(candidate_prior.left_y)) > stick_deadzone;
    }
    if (player_ == no_device) {
        return frame;
    }
    frame.connected = true;
    frame.player = player_;
    const auto& sample = samples[player_];
    if (!enabled || !armed_) {
        if (enabled && !meaningful(sample, profile)) {
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
        blocked_left_x_ = std::abs(static_cast<int>(sample.left_x)) > stick_deadzone;
        effective.left_x = 0;
    }
    if (blocked_left_y_) {
        blocked_left_y_ = std::abs(static_cast<int>(sample.left_y)) > stick_deadzone;
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
    return poll(enabled, runtime_backend().profile);
}

Frame poll(bool enabled, const controller::Profile& profile) {
    auto& backend = runtime_backend();
    static thread_local Selection selection;
    if (enabled) {
        backend.profile = controller::normalize(profile);
    }
    return selection.poll(backend, enabled, backend.profile);
}

}
