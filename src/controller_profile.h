#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace controller {

enum class Action {
    Activate,
    Inventory,
    Examine,
    Back,
    Skip,
    Menu,
    Previous,
    Next,
    Evidence,
    Speed,
    Aim,
    Targets,
    Count
};
enum class Binding {
    A,
    B,
    X,
    Y,
    Back,
    Start,
    LeftShoulder,
    RightShoulder,
    LeftThumb,
    RightThumb,
    LeftTrigger,
    RightTrigger,
    Count
};
enum class Curve { Linear, Quadratic, Cubic };
inline constexpr auto action_count = static_cast<std::size_t>(Action::Count);
inline constexpr auto binding_count = static_cast<std::size_t>(Binding::Count);
inline constexpr std::array action_keys{L"Activate", L"Inventory", L"Examine",  L"Back",
                                        L"Skip",     L"Menu",      L"Previous", L"Next",
                                        L"Evidence", L"Speed",     L"Aim",      L"Targets"};
inline constexpr std::array binding_names{L"A",  L"B",  L"X",  L"Y",  L"Back", L"Start",
                                          L"LB", L"RB", L"L3", L"R3", L"LT",   L"RT"};
inline constexpr std::array curve_names{L"Linear", L"Quadratic", L"Cubic"};
inline constexpr std::array<std::uint16_t, binding_count> binding_masks{
    0x1000, 0x2000, 0x4000, 0x8000, 0x0020, 0x0010, 0x0100, 0x0200, 0x0040, 0x0080, 0, 0};

struct Profile {
    std::array<Binding, action_count> bindings{Binding::A,
                                               Binding::B,
                                               Binding::X,
                                               Binding::Y,
                                               Binding::Back,
                                               Binding::Start,
                                               Binding::LeftShoulder,
                                               Binding::RightShoulder,
                                               Binding::RightThumb,
                                               Binding::LeftThumb,
                                               Binding::LeftTrigger,
                                               Binding::RightTrigger};
    unsigned deadzone = 7849;
    unsigned sensitivity = 100;
    Curve curve = Curve::Quadratic;
    bool invert_x = false;
    bool invert_y = false;
    unsigned trigger_threshold = 30;

    bool operator==(const Profile&) const = default;
};

inline Profile normalize(Profile profile) {
    const Profile defaults;
    std::array<bool, binding_count> used{};
    bool valid = true;
    for (const auto binding : profile.bindings) {
        const auto index = static_cast<std::size_t>(binding);
        if (index >= binding_count || used[index]) {
            valid = false;
            break;
        }
        used[index] = true;
    }
    if (!valid) {
        profile.bindings = defaults.bindings;
    }
    if (profile.deadzone > 30000) {
        profile.deadzone = defaults.deadzone;
    }
    if (profile.sensitivity < 25 || profile.sensitivity > 300) {
        profile.sensitivity = defaults.sensitivity;
    }
    if (static_cast<unsigned>(profile.curve) > static_cast<unsigned>(Curve::Cubic)) {
        profile.curve = defaults.curve;
    }
    if (profile.trigger_threshold > 254) {
        profile.trigger_threshold = defaults.trigger_threshold;
    }
    return profile;
}

inline void bind(Profile& profile, Action action, Binding binding) {
    profile = normalize(profile);
    const auto index = static_cast<std::size_t>(action);
    if (index >= action_count || static_cast<std::size_t>(binding) >= binding_count) {
        return;
    }
    const auto other = std::find(profile.bindings.begin(), profile.bindings.end(), binding);
    std::swap(profile.bindings[index], *other);
}

inline bool held(Binding binding, std::uint16_t buttons, std::uint8_t left_trigger,
                 std::uint8_t right_trigger, unsigned threshold) {
    if (binding == Binding::LeftTrigger) {
        return left_trigger > threshold;
    }
    if (binding == Binding::RightTrigger) {
        return right_trigger > threshold;
    }
    const auto index = static_cast<std::size_t>(binding);
    return index < binding_count && (buttons & binding_masks[index]) != 0;
}

inline float axis(std::int16_t value, const Profile& requested, bool spring = false) {
    const auto profile = normalize(requested);
    const auto magnitude = std::abs(static_cast<int>(value));
    if (magnitude <= static_cast<int>(profile.deadzone)) {
        return 0;
    }
    auto normalized =
        std::min(1.0f, float(magnitude - profile.deadzone) / (32767 - profile.deadzone));
    // Spring controls displacement. The curve controls conventional pointer velocity.
    if (!spring && profile.curve != Curve::Linear) {
        normalized *= normalized;
        if (profile.curve == Curve::Cubic) {
            normalized *=
                std::min(1.0f, float(magnitude - profile.deadzone) / (32767 - profile.deadzone));
        }
    }
    normalized *= profile.sensitivity / 100.0f;
    return std::copysign(spring ? std::min(1.0f, normalized) : normalized,
                         static_cast<float>(value));
}

}
