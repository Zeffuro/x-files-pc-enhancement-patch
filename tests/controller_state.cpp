#include "enhancements/controller_state.h"

#include <array>
#include <stdexcept>

namespace {

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("controller state regression");
    }
}

struct FakeBackend : enhancements::input::Backend {
    std::array<bool, enhancements::input::max_devices> connected{};
    std::array<enhancements::input::Sample, enhancements::input::max_devices> samples{};

    bool read(unsigned player, enhancements::input::Sample& sample) override {
        if (!connected[player]) {
            return false;
        }
        sample = samples[player];
        return true;
    }
};

void held_input_after_suspend() {
    FakeBackend backend{};
    enhancements::input::Selection selection;
    backend.connected[0] = true;
    auto frame = selection.poll(backend, true);
    require(frame.connected && frame.player == 0 && frame.device_changed);
    backend.samples[0].buttons = enhancements::input::button::activate;
    frame = selection.poll(backend, true);
    require(frame.pressed == enhancements::input::button::activate);
    require(selection.poll(backend, true).pressed == 0);
    require(selection.poll(backend, false).pressed == 0);
    frame = selection.poll(backend, true);
    require(frame.pressed == 0 && frame.sample.buttons == 0);
    backend.samples[0].buttons = 0;
    selection.poll(backend, true);
    backend.samples[0].buttons = enhancements::input::button::activate;
    require(selection.poll(backend, true).pressed == enhancements::input::button::activate);

    backend.samples[0].left_trigger = 255;
    require(selection.poll(backend, false).aim_pressed == false);
    require(selection.poll(backend, true).aim_pressed == false);
    backend.samples[0] = {};
    selection.poll(backend, true);
    backend.samples[0].left_trigger = 255;
    require(selection.poll(backend, true).aim_pressed);
}

void active_device_and_hotplug() {
    FakeBackend backend{};
    enhancements::input::Selection selection;
    backend.connected[0] = backend.connected[1] = true;
    selection.poll(backend, true);
    backend.samples[1].buttons = enhancements::input::button::inventory;
    auto frame = selection.poll(backend, true);
    require(frame.player == 1 && frame.device_changed &&
            frame.pressed == enhancements::input::button::inventory);
    require(selection.poll(backend, true).pressed == 0);
    backend.samples[1].buttons = 0;
    selection.poll(backend, true);
    backend.samples[1].buttons = enhancements::input::button::inventory;
    require(selection.poll(backend, true).pressed == enhancements::input::button::inventory);

    backend.connected[1] = false;
    backend.samples[0].buttons = enhancements::input::button::activate;
    frame = selection.poll(backend, true);
    require(frame.player == 0 && frame.device_changed && frame.pressed == 0);
    require(selection.poll(backend, true).pressed == 0);
    backend.samples[0].buttons = 0;
    selection.poll(backend, true);
    backend.samples[0].buttons = enhancements::input::button::activate;
    require(selection.poll(backend, true).pressed == enhancements::input::button::activate);

    backend.connected[1] = true;
    backend.samples[1].buttons = enhancements::input::button::inventory;
    frame = selection.poll(backend, true);
    require(frame.player == 0 && !frame.device_changed);
    backend.samples[1].buttons = 0;
    selection.poll(backend, true);
    backend.samples[1].buttons = enhancements::input::button::inventory;
    frame = selection.poll(backend, true);
    require(frame.player == 1 && frame.device_changed &&
            frame.pressed == enhancements::input::button::inventory);
    backend.samples[1].buttons = 0;
    selection.poll(backend, true);
    backend.samples[1].buttons = enhancements::input::button::inventory;
    require(selection.poll(backend, true).pressed == enhancements::input::button::inventory);
}

void no_phantom_analog_after_switch() {
    FakeBackend backend{};
    enhancements::input::Selection selection;
    backend.connected[0] = backend.connected[1] = true;
    selection.poll(backend, true);
    backend.samples[1].left_x = 24000;
    auto frame = selection.poll(backend, true);
    require(frame.player == 1 && frame.sample.left_x == 24000);
    require(selection.poll(backend, true).sample.left_x == 24000);
    backend.samples[1].left_x = 0;
    selection.poll(backend, true);
    backend.samples[1].left_x = 24000;
    require(selection.poll(backend, true).sample.left_x == 24000);
}

void switching_preserves_existing_held_baseline() {
    FakeBackend backend{};
    enhancements::input::Selection selection;
    backend.connected[0] = backend.connected[1] = true;
    selection.poll(backend, true);
    backend.samples[1].buttons = enhancements::input::button::activate;
    backend.samples[1].left_trigger = 255;
    backend.samples[1].right_trigger = 255;
    backend.samples[1].left_x = 24000;
    backend.samples[1].left_y = -24000;
    selection.poll(backend, false);
    selection.poll(backend, true);
    backend.samples[1].buttons |= enhancements::input::button::back;
    const auto frame = selection.poll(backend, true);
    require(frame.player == 1 && frame.device_changed);
    require(frame.pressed == enhancements::input::button::back);
    require(!frame.aim_pressed);
    require(frame.sample.buttons == enhancements::input::button::back);
    require(frame.sample.left_trigger == 0 && frame.sample.right_trigger == 0);
    require(frame.sample.left_x == 0 && frame.sample.left_y == 0);
    require(selection.poll(backend, true).sample.buttons == enhancements::input::button::back);
    backend.samples[1] = {};
    selection.poll(backend, true);
    backend.samples[1].buttons = enhancements::input::button::activate;
    backend.samples[1].left_trigger = 255;
    backend.samples[1].left_x = 24000;
    const auto fresh = selection.poll(backend, true);
    require(fresh.pressed == enhancements::input::button::activate && fresh.aim_pressed);
    require(fresh.sample.left_x == 24000);
}

}

int main() {
    held_input_after_suspend();
    active_device_and_hotplug();
    no_phantom_analog_after_switch();
    switching_preserves_existing_held_baseline();
}
