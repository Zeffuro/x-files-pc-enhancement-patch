#include "controller_profile.h"

#include <stdexcept>

namespace {

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("portable controller profile regression");
    }
}

}

int main() {
    using namespace controller;
    const Profile defaults;
    for (std::size_t action = 0; action < action_count; ++action) {
        for (std::size_t binding = 0; binding < binding_count; ++binding) {
            auto profile = defaults;
            bind(profile, static_cast<Action>(action), static_cast<Binding>(binding));
            require(profile.bindings[action] == static_cast<Binding>(binding));
            require(normalize(profile) == profile);
            bind(profile, static_cast<Action>(action), defaults.bindings[action]);
            require(profile == defaults);
        }
    }
    for (const auto invalid : {30001u, 32767u, 32768u, 0xffffffffu}) {
        auto profile = defaults;
        profile.deadzone = invalid;
        require(normalize(profile) == defaults);
        require(axis(-32768, profile) == -1);
    }
    auto profile = defaults;
    profile.deadzone = 0;
    profile.sensitivity = 300;
    require(axis(-32768, profile) == -3 && axis(32767, profile) == 3);
    require(axis(-32768, profile, true) == -1 && axis(32767, profile, true) == 1);
    for (const auto deadzone : {0u, 7849u, 30000u}) {
        for (const auto curve : {Curve::Linear, Curve::Quadratic, Curve::Cubic}) {
            profile = defaults;
            profile.deadzone = deadzone;
            profile.curve = curve;
            float previous = 0;
            for (int value = 0; value <= 32767; ++value) {
                const auto current = axis(static_cast<std::int16_t>(value), profile);
                require(current >= previous && current >= 0 && current <= 1);
                require(value > static_cast<int>(deadzone) || current == 0);
                require(axis(static_cast<std::int16_t>(-value), profile) == -current);
                previous = current;
            }
            require(previous == 1);
        }
    }
}
