#include "enhancements/controller_navigation.h"

#include <cmath>
#include <stdexcept>

namespace {

using enhancements::input::capped_elapsed;
using enhancements::input::cursor_axis;
using enhancements::input::left_deadzone;
using enhancements::input::MotionAccumulator;
using enhancements::input::NavigationRepeat;
using enhancements::input::Sample;
using enhancements::input::button::left;
using enhancements::input::button::right;
using enhancements::input::button::up;

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("controller navigation regression");
    }
}

void axis_and_motion() {
    require(cursor_axis(0) == 0);
    require(cursor_axis(left_deadzone) == 0);
    require(cursor_axis(-left_deadzone) == 0);
    require(cursor_axis(32767) == 1);
    require(cursor_axis(-32768) == -1);
    const float midpoint = cursor_axis(static_cast<std::int16_t>((left_deadzone + 32767) / 2));
    require(midpoint > 0.24f && midpoint < 0.26f);

    MotionAccumulator motion;
    require(motion.advance(5, -5, 0.1f).x == 0);
    const auto first = motion.advance(5, -5, 0.1f);
    require(first.x == 1 && first.y == -1);
    require(motion.advance(-5, 5, 0.1f).x == 0);
    const auto reversed = motion.advance(-5, 5, 0.1f);
    require(reversed.x == -1 && reversed.y == 1);
    motion.advance(5, 5, 0.1f);
    motion.reset();
    require(motion.advance(5, 5, 0.1f).x == 0);
}

void elapsed_cap() {
    std::uint64_t previous = 0;
    require(capped_elapsed(1000, previous) == 0 && previous == 1000);
    require(std::abs(capped_elapsed(1016, previous) - 0.016f) < 0.00001f);
    require(capped_elapsed(2016, previous) == 0.05f && previous == 2016);
    require(capped_elapsed(2000, previous) == 0 && previous == 2000);
}

void navigation_repeat() {
    NavigationRepeat repeat;
    Sample sample{};
    sample.buttons = right;
    auto nav = repeat.update(sample, false, false, false, 1000);
    require(nav.horizontal == 1 && nav.vertical == 0 && nav.step);
    require(!repeat.update(sample, false, false, false, 1349).step);
    require(repeat.update(sample, false, false, false, 1350).step);
    require(!repeat.update(sample, false, false, false, 1489).step);
    require(repeat.update(sample, false, false, false, 1490).step);

    sample.buttons = left | up;
    nav = repeat.update(sample, false, false, false, 1500);
    require(nav.horizontal == -1 && nav.vertical == -1 && nav.step);
    sample.buttons = 0;
    nav = repeat.update(sample, false, false, false, 1501);
    require(nav.horizontal == 0 && nav.vertical == 0 && nav.step);
    sample.buttons = right;
    require(repeat.update(sample, false, false, false, 1502).step);
    repeat.reset();
    require(repeat.update(sample, false, false, false, 1503).step);
    require(!repeat.update(sample, false, false, false, 1504).step);
}

void stick_navigation_modes() {
    NavigationRepeat repeat;
    Sample sample{};
    sample.left_x = 18000;
    sample.left_y = -18000;
    auto nav = repeat.update(sample, false, false, false, 100);
    require(nav.horizontal == 0 && nav.vertical == 0);
    sample.left_x = 18001;
    sample.left_y = -18001;
    nav = repeat.update(sample, false, false, false, 101);
    require(nav.horizontal == 1 && nav.vertical == 1 && nav.step);
    nav = repeat.update(sample, true, false, false, 102);
    require(nav.horizontal == 0 && nav.vertical == 0 && nav.step);
    nav = repeat.update(sample, true, true, false, 103);
    require(nav.horizontal == 1 && nav.vertical == 1 && nav.step);
    nav = repeat.update(sample, true, true, true, 104);
    require(nav.horizontal == 0 && nav.vertical == 0 && nav.step);
}

}

int main() {
    axis_and_motion();
    elapsed_cap();
    navigation_repeat();
    stick_navigation_modes();
}
