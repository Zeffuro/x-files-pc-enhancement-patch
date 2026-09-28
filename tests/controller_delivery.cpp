#include "enhancements/input_delivery.h"
#include "enhancements/controller_context.h"

#include <deque>
#include <stdexcept>
#include <vector>

namespace {
using namespace enhancements::input;

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("controller delivery regression");
    }
}

struct FakeDelivery : DeliveryBackend {
    std::array<bool, 256> physical{};
    std::array<bool, 256> down{};
    std::deque<unsigned> limits;
    std::vector<std::vector<INPUT>> calls;

    unsigned submit(std::span<INPUT> events) override {
        calls.emplace_back(events.begin(), events.end());
        const auto limit = limits.empty() ? static_cast<unsigned>(events.size()) : limits.front();
        if (!limits.empty()) {
            limits.pop_front();
        }
        const auto sent = std::min(limit, static_cast<unsigned>(events.size()));
        for (unsigned i = 0; i < sent; ++i) {
            const auto& event = events[i];
            if (event.type == INPUT_KEYBOARD) {
                down[event.ki.wVk] = !(event.ki.dwFlags & KEYEVENTF_KEYUP);
            } else {
                const bool right = event.mi.dwFlags & (MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP);
                down[right ? VK_RBUTTON : VK_LBUTTON] =
                    !(event.mi.dwFlags & (MOUSEEVENTF_LEFTUP | MOUSEEVENTF_RIGHTUP));
            }
        }
        return sent;
    }

    bool held(WORD key) override {
        return physical[key] || down[key];
    }
};

void mouse_recovery() {
    for (bool right : {false, true}) {
        FakeDelivery backend;
        Delivery delivery(backend);
        backend.limits = {1, 0, 0, 1};
        require(delivery.click(right, 1234).started());
        require(delivery.pending());
        require(!delivery.key(VK_ESCAPE, 0, 5678).started());
        require(delivery.pending());
        require(delivery.recover());
        require(!backend.held(right ? VK_RBUTTON : VK_LBUTTON));
        require(backend.calls.size() == 4);
        for (unsigned i = 1; i < backend.calls.size(); ++i) {
            require(backend.calls[i].size() == 1);
            require(backend.calls[i][0].mi.dwExtraInfo == 1234);
            require(backend.calls[i][0].mi.dwFlags ==
                    static_cast<DWORD>(right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP));
        }
    }
}

void every_chord_prefix() {
    for (unsigned prefix = 0; prefix <= 8; ++prefix) {
        FakeDelivery backend;
        Delivery delivery(backend);
        backend.limits = {prefix, 0};
        const auto result = delivery.key('A', 7, 4321);
        require(result.started() == (prefix > 3));
        require(delivery.pending() == (prefix > 0 && prefix < 8));
        backend.limits.clear();
        require(delivery.recover());
        for (auto key : {VK_SHIFT, VK_CONTROL, VK_MENU, int('A')}) {
            require(!backend.held(static_cast<WORD>(key)));
        }
        for (unsigned i = 1; i < backend.calls.size(); ++i) {
            for (const auto& event : backend.calls[i]) {
                require(event.ki.dwFlags == KEYEVENTF_KEYUP);
                require(event.ki.dwExtraInfo == 4321);
            }
        }
    }
}

void interrupted_release() {
    FakeDelivery backend;
    Delivery delivery(backend);
    backend.limits = {4, 2, 0, 1, 1};
    require(delivery.key('B', 7, 9).started());
    require(delivery.pending());
    require(!backend.held('B') && !backend.held(VK_MENU));
    require(backend.held(VK_CONTROL) && backend.held(VK_SHIFT));
    require(!delivery.recover());
    require(!delivery.recover());
    require(!backend.held(VK_CONTROL) && backend.held(VK_SHIFT));
    require(delivery.recover());
    require(!backend.held(VK_SHIFT));
}

void physical_input_is_not_released() {
    FakeDelivery backend;
    Delivery delivery(backend);
    backend.physical[VK_SHIFT] = true;
    require(delivery.key('A', 1, 7).started());
    require(backend.calls.size() == 1 && backend.calls[0].size() == 2);
    require(backend.calls[0][0].ki.wVk == 'A' && backend.held(VK_SHIFT));
    backend.physical[VK_LBUTTON] = true;
    backend.physical[VK_ESCAPE] = true;
    require(!delivery.click(false, 7).started());
    require(!delivery.key(VK_ESCAPE, 0, 7).started());
    require(backend.calls.size() == 1 && backend.held(VK_LBUTTON) && backend.held(VK_ESCAPE));
}

void context_barriers() {
    ContextBarrier barrier;
    Context world;
    Context dialogue{ContextKind::dialogue};
    Frame frame;
    require(!barrier.filter(world, frame));
    const Sample held{button::activate | button::right, 255, 255, 23000, -23000};
    frame.sample = held;
    frame.pressed = button::activate;
    frame.aim_pressed = true;
    require(barrier.filter(dialogue, frame));
    require(!frame.pressed && !frame.aim_pressed && !frame.sample.buttons);
    require(!frame.sample.left_x && !frame.sample.left_y && !frame.sample.left_trigger &&
            !frame.sample.right_trigger);
    frame.sample = held;
    frame.sample.buttons |= button::back;
    frame.pressed = button::back;
    require(!barrier.filter(dialogue, frame));
    require(frame.pressed == button::back && frame.sample.buttons == button::back);
    frame = {};
    barrier.filter(dialogue, frame);
    frame.sample = held;
    frame.pressed = button::activate;
    frame.aim_pressed = true;
    barrier.filter(dialogue, frame);
    require(frame.pressed == button::activate && frame.aim_pressed && frame.sample.left_x == 23000);

    Context script{ContextKind::script, 0, {123}};
    frame = {};
    require(barrier.filter(script, frame));
    script.resources = {456};
    frame.sample.buttons = button::down;
    require(barrier.filter(script, frame) && !frame.sample.buttons);
    frame = {};
    frame.device_changed = true;
    frame.sample.buttons = frame.pressed = button::activate;
    require(!barrier.filter(script, frame));
    require(frame.pressed == button::activate);
    barrier.reset();
    require(!barrier.filter(world, frame));
}
}

int main() {
    mouse_recovery();
    every_chord_prefix();
    interrupted_release();
    physical_input_is_not_released();
    context_barriers();
}
