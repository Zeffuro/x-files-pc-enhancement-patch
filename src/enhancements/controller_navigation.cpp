#include "controller_navigation.h"

#include <algorithm>
#include <cmath>

namespace enhancements::input {

float cursor_axis(std::int16_t value) {
    const int magnitude = std::abs(static_cast<int>(value));
    if (magnitude <= left_deadzone) {
        return 0;
    }
    const auto normalized =
        std::min(1.0f, float(magnitude - left_deadzone) / (32767 - left_deadzone));
    return std::copysign(normalized * normalized, static_cast<float>(value));
}

float capped_elapsed(std::uint64_t now, std::uint64_t& previous) {
    const auto elapsed =
        previous && now >= previous ? std::min(0.05f, (now - previous) / 1000.0f) : 0.0f;
    previous = now;
    return elapsed;
}

Motion MotionAccumulator::advance(float velocity_x, float velocity_y, float elapsed) {
    remainder_x_ += velocity_x * elapsed;
    remainder_y_ += velocity_y * elapsed;
    const Motion motion{static_cast<int>(remainder_x_), static_cast<int>(remainder_y_)};
    remainder_x_ -= motion.x;
    remainder_y_ -= motion.y;
    return motion;
}

void MotionAccumulator::reset() {
    remainder_x_ = remainder_y_ = 0;
}

NavigationStep NavigationRepeat::update(const Sample& sample, bool analog_cursor, bool jump_mode,
                                        bool aim_mode, std::uint64_t now) {
    const auto buttons = sample.buttons;
    const bool stick_navigation = !analog_cursor || (jump_mode && !aim_mode);
    const auto horizontal_input = (buttons & button::right ? 1 : 0) -
                                  (buttons & button::left ? 1 : 0) +
                                  (stick_navigation && sample.left_x > 18000 ? 1 : 0) -
                                  (stick_navigation && sample.left_x < -18000 ? 1 : 0);
    const auto vertical_input = (buttons & button::down ? 1 : 0) - (buttons & button::up ? 1 : 0) +
                                (!analog_cursor && sample.left_y < -18000 ? 1 : 0) -
                                (!analog_cursor && sample.left_y > 18000 ? 1 : 0);
    const int horizontal = std::clamp(horizontal_input, -1, 1);
    const int vertical = std::clamp(vertical_input, -1, 1);
    const bool changed = horizontal != previous_horizontal_ || vertical != previous_vertical_;
    const bool step = changed || now >= repeat_at_;
    if (step) {
        repeat_at_ = now + (changed ? 350 : 140);
    }
    previous_horizontal_ = horizontal;
    previous_vertical_ = vertical;
    return {horizontal, vertical, step};
}

void NavigationRepeat::reset() {
    previous_horizontal_ = previous_vertical_ = 0;
    repeat_at_ = 0;
}

}
