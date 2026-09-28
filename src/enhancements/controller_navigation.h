#pragma once

#include "controller_state.h"

#include <cstdint>

namespace enhancements::input {

float cursor_axis(std::int16_t value);
float capped_elapsed(std::uint64_t now, std::uint64_t& previous);

struct Motion {
    int x = 0;
    int y = 0;
};

class MotionAccumulator {
public:
    Motion advance(float velocity_x, float velocity_y, float elapsed);
    void reset();

private:
    float remainder_x_ = 0;
    float remainder_y_ = 0;
};

struct NavigationStep {
    int horizontal = 0;
    int vertical = 0;
    bool step = false;
};

class NavigationRepeat {
public:
    NavigationStep update(const Sample& sample, bool analog_cursor, bool jump_mode, bool aim_mode,
                          std::uint64_t now);
    void reset();

private:
    int previous_horizontal_ = 0;
    int previous_vertical_ = 0;
    std::uint64_t repeat_at_ = 0;
};

}
