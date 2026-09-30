#pragma once

#include "controller_state.h"
#include <cmath>
#include <vector>

namespace enhancements::input {

enum class ContextKind {
    world,
    native,
    menu,
    modal,
    script,
    script_dialog,
    dialogue,
    inventory,
    emotions,
    browser
};

struct Context {
    ContextKind kind = ContextKind::world;
    std::uintptr_t native = 0;
    std::vector<unsigned> resources;
    bool history = false;

    bool operator==(const Context&) const = default;
};

class ContextBarrier {
public:
    bool filter(const Context& context, Frame& frame, const controller::Profile& profile = {}) {
        if (frame.device_changed) {
            blocked_ = {};
        }
        const bool changed = known_ && context != previous_;
        if (changed) {
            blocked_ = frame.sample;
        }
        previous_ = context;
        known_ = true;
        auto& sample = frame.sample;
        blocked_.buttons &= sample.buttons;
        sample.buttons &= ~blocked_.buttons;
        frame.pressed &= sample.buttons;
        filter_trigger(blocked_.left_trigger, sample.left_trigger);
        filter_trigger(blocked_.right_trigger, sample.right_trigger);
        filter_axis(blocked_.left_x, sample.left_x, profile.deadzone);
        filter_axis(blocked_.left_y, sample.left_y, profile.deadzone);
        frame.aim_pressed &= sample.left_trigger > trigger_threshold;
        return changed;
    }

    void reset() {
        known_ = false;
        blocked_ = {};
    }

private:
    static void filter_trigger(std::uint8_t& blocked, std::uint8_t& value) {
        if (blocked > trigger_threshold) {
            blocked = value;
            value = 0;
        }
    }

    static void filter_axis(std::int16_t& blocked, std::int16_t& value, unsigned deadzone) {
        if (std::abs(static_cast<int>(blocked)) > static_cast<int>(deadzone)) {
            blocked = value;
            value = 0;
        }
    }

    Context previous_;
    Sample blocked_{};
    bool known_ = false;
};

}
