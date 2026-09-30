#include "controller_profile.h"
#include "enhancements/controller_state.h"
#include "enhancements/controller_navigation.h"
#include "enhancements/controller_context.h"
#include "enhancements/spring_cursor.h"
#include "settings.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <windows.h>

namespace {

using namespace controller;
using namespace enhancements::input;

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("controller profile regression");
    }
}

struct FakeBackend final : Backend {
    std::array<bool, max_devices> connected{};
    std::array<RawState, max_devices> raw{};
    Profile profile;

    bool read(unsigned player, Sample& sample) override {
        sample = map_sample(raw[player], profile);
        return connected[player];
    }
};

void default_mapping_and_swaps() {
    constexpr std::array<std::uint16_t, action_count> logical{button::activate,
                                                              button::inventory,
                                                              button::examine,
                                                              button::back,
                                                              button::skip,
                                                              button::menu,
                                                              button::previous,
                                                              button::next,
                                                              button::evidence,
                                                              button::speed,
                                                              0,
                                                              0};
    const Profile defaults;
    for (std::size_t action = 0; action < action_count; ++action) {
        RawState raw;
        const auto binding = defaults.bindings[action];
        raw.buttons = binding_masks[static_cast<std::size_t>(binding)];
        raw.left_trigger = binding == Binding::LeftTrigger ? 255 : 0;
        raw.right_trigger = binding == Binding::RightTrigger ? 255 : 0;
        const auto sample = map_sample(raw, defaults);
        require(sample.buttons == logical[action]);
        require(sample.left_trigger == (action == 10 ? 255 : 0));
        require(sample.right_trigger == (action == 11 ? 255 : 0));
        for (std::size_t physical = 0; physical < binding_count; ++physical) {
            auto profile = defaults;
            bind(profile, static_cast<Action>(action), static_cast<Binding>(physical));
            require(profile == normalize(profile));
            require(profile.bindings[action] == static_cast<Binding>(physical));
            const auto other = std::find(defaults.bindings.begin(), defaults.bindings.end(),
                                         static_cast<Binding>(physical)) -
                               defaults.bindings.begin();
            require(profile.bindings[other] == binding);
            RawState mapped;
            mapped.buttons = binding_masks[physical];
            mapped.left_trigger = physical == 10 ? 255 : 0;
            mapped.right_trigger = physical == 11 ? 255 : 0;
            const auto result = map_sample(mapped, profile);
            require(result.buttons == logical[action]);
            require(result.left_trigger == (action == 10 ? 255 : 0));
            require(result.right_trigger == (action == 11 ? 255 : 0));
        }
    }
    for (unsigned value = 0; value < 256; ++value) {
        RawState raw;
        raw.left_trigger = raw.right_trigger = static_cast<std::uint8_t>(value);
        const auto sample = map_sample(raw, defaults);
        require(sample.left_trigger == value && sample.right_trigger == value);
    }
    require(map_sample({0xF, 0, 0, -32768, 32767}, defaults).buttons ==
            (button::up | button::down | button::left | button::right));
    Profile invalid = defaults;
    invalid.bindings[0] = Binding::Count;
    require(normalize(invalid) == defaults);
    invalid = defaults;
    invalid.bindings[0] = Binding::B;
    require(normalize(invalid) == defaults);
}

void calibration_and_navigation() {
    Profile profile;
    for (const auto threshold : {0u, 30u, 254u}) {
        profile.trigger_threshold = threshold;
        RawState raw;
        raw.left_trigger = static_cast<std::uint8_t>(threshold);
        require(map_sample(raw, profile).left_trigger <= trigger_threshold);
        raw.left_trigger = static_cast<std::uint8_t>(threshold + 1);
        require(map_sample(raw, profile).left_trigger > trigger_threshold);
        bind(profile, Action::Activate, Binding::LeftTrigger);
        require(map_sample(raw, profile).buttons == button::activate);
        profile = {};
    }
    profile.deadzone = 0;
    profile.curve = Curve::Linear;
    require(cursor_axis(1, profile) > 0 && cursor_axis(0, profile) == 0);
    profile.deadzone = 30000;
    require(cursor_axis(30000, profile) == 0 && cursor_axis(30001, profile) > 0);
    profile.sensitivity = 300;
    require(cursor_axis(32767, profile) == 3 && cursor_axis(-32768, profile) == -3);
    profile.sensitivity = 25;
    require(cursor_axis(32767, profile) == 0.25f);
    profile = {};
    const auto midpoint = static_cast<std::int16_t>((profile.deadzone + 32767) / 2);
    for (const auto curve : {Curve::Linear, Curve::Quadratic, Curve::Cubic}) {
        profile.curve = curve;
        const auto expected = curve == Curve::Linear      ? 0.5f
                              : curve == Curve::Quadratic ? 0.25f
                                                          : 0.125f;
        require(std::abs(cursor_axis(midpoint, profile) - expected) < 0.001f);
    }
    profile.invert_x = profile.invert_y = true;
    const auto inverted = map_sample({0, 0, 0, -32768, 18001}, profile);
    require(inverted.left_x == 32767 && inverted.left_y == -18001);
    NavigationRepeat navigation;
    require(navigation.update(inverted, false, false, false, 100, profile).vertical == 1);
    profile.deadzone = 30000;
    require(navigation.update(inverted, false, false, false, 101, profile).vertical == 0);

    enhancements::SpringCursor spring;
    POINT position{};
    RECT bounds{0, 0, 640, 480};
    profile = {};
    require(spring.update(midpoint, 0, bounds, position, profile));
    require(position.x >= 479 && position.x <= 480 && position.y == 240);
    profile.sensitivity = 25;
    require(spring.update(32767, 0, bounds, position, profile) && position.x == 400);
    profile.sensitivity = 300;
    require(spring.update(32767, 0, bounds, position, profile) && position.x == 639);
}

void profile_barriers() {
    FakeBackend backend;
    backend.connected[0] = true;
    Selection selection;
    selection.poll(backend, true, backend.profile);
    backend.raw[0] = {0x1000, 255, 255, 22000, -22000};
    require(selection.poll(backend, true, backend.profile).pressed == button::activate);
    bind(backend.profile, Action::Activate, Binding::B);
    auto frame = selection.poll(backend, true, backend.profile);
    require(frame.device_changed && frame.connected && !frame.pressed && !frame.aim_pressed);
    require(!frame.sample.buttons && !frame.sample.left_trigger && !frame.sample.left_x);
    require(!selection.poll(backend, true, backend.profile).sample.buttons);
    backend.raw[0] = {};
    selection.poll(backend, true, backend.profile);
    backend.raw[0].buttons = 0x2000;
    require(selection.poll(backend, true, backend.profile).pressed == button::activate);
    backend.profile.trigger_threshold = 0;
    backend.raw[0].left_trigger = 1;
    require(!selection.poll(backend, true, backend.profile).sample.buttons);
    backend.raw[0] = {};
    selection.poll(backend, true, backend.profile);
    backend.raw[0].left_trigger = 1;
    require(selection.poll(backend, true, backend.profile).aim_pressed);
    backend.connected[0] = false;
    selection.poll(backend, true, backend.profile);
    backend.connected[0] = true;
    require(!selection.poll(backend, true, backend.profile).aim_pressed);

    ContextBarrier barrier;
    Profile small;
    small.deadzone = 0;
    Context context;
    frame = {};
    frame.sample.left_x = 100;
    barrier.filter(context, frame, small);
    context.kind = ContextKind::menu;
    require(barrier.filter(context, frame, small) && frame.sample.left_x == 0);
    frame.sample.left_x = 100;
    barrier.filter(context, frame, small);
    require(frame.sample.left_x == 0);
    frame.sample.left_x = 0;
    barrier.filter(context, frame, small);
    frame.sample.left_x = 100;
    barrier.filter(context, frame, small);
    require(frame.sample.left_x == 100);
}

void persistence() {
    wchar_t executable[32768]{};
    require(GetModuleFileNameW(nullptr, executable, _countof(executable)) != 0);
    const auto path = std::filesystem::path(executable).parent_path() / L"patch.ini";
    const auto backup = path.wstring() + L".controller-test-backup";
    const bool existed = std::filesystem::exists(path);
    require(!std::filesystem::exists(backup));
    if (existed) {
        require(CopyFileW(path.c_str(), backup.c_str(), TRUE) != FALSE);
    }

    struct Restore {
        std::filesystem::path path, backup;
        bool existed;

        ~Restore() {
            if (existed) {
                CopyFileW(backup.c_str(), path.c_str(), FALSE);
            } else {
                DeleteFileW(path.c_str());
            }
            DeleteFileW(backup.c_str());
            WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
        }
    } restore{path, backup, existed};

    std::ofstream(path) << "[Controller]\n";
    require(read_settings(path).controller_profile == Profile{});
    Settings desired;
    bind(desired.controller_profile, Action::Menu, Binding::RightTrigger);
    bind(desired.controller_profile, Action::Activate, Binding::LeftThumb);
    desired.controller_profile.deadzone = 30000;
    desired.controller_profile.sensitivity = 25;
    desired.controller_profile.curve = Curve::Cubic;
    desired.controller_profile.trigger_threshold = 254;
    desired.controller_profile.invert_x = desired.controller_profile.invert_y = true;
    save_settings(desired);
    require(read_settings(path).controller_profile == desired.controller_profile);
    require(settings().controller_profile == desired.controller_profile);
    constexpr std::array keys{"Deadzone", "Sensitivity", "Curve", "TriggerThreshold", "Activate"};
    for (const auto* key : keys) {
        for (const auto* invalid : {"-1", "99999999999999999999999999", "nonsense", "4294967295"}) {
            std::ofstream(path) << "[Controller]\n" << key << '=' << invalid << '\n';
            WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
            require(read_settings(path).controller_profile == Profile{});
        }
    }
    std::ofstream(path) << "[Controller]\nActivate=1\nInventory=1\nInvertX=2\nInvertY=-1\n";
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    require(read_settings(path).controller_profile == Profile{});
}

}

int main() {
    default_mapping_and_swaps();
    calibration_and_navigation();
    profile_barriers();
    persistence();
}
