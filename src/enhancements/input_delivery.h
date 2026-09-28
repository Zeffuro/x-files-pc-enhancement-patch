#pragma once

#include <windows.h>
#include <algorithm>
#include <array>
#include <span>

namespace enhancements::input {

struct DeliveryResult {
    unsigned inserted = 0;
    unsigned activation = 0;

    bool started() const {
        return inserted > activation;
    }
};

class DeliveryBackend {
public:
    virtual ~DeliveryBackend() = default;
    virtual unsigned submit(std::span<INPUT> events) = 0;
    virtual bool held(WORD key) = 0;
};

class Delivery {
public:
    explicit Delivery(DeliveryBackend& backend) : backend_(backend) {}

    bool pending() const {
        return count_ != 0;
    }

    bool recover() {
        if (!count_) {
            return true;
        }
        std::array<INPUT, 4> events{};
        for (unsigned i = 0; i < count_; ++i) {
            events[i] = releases_[count_ - i - 1];
        }
        const auto sent = std::min(count_, backend_.submit({events.data(), count_}));
        count_ -= sent;
        return !count_;
    }

    DeliveryResult click(bool right, ULONG_PTR tag) {
        if (!recover() || backend_.held(right ? VK_RBUTTON : VK_LBUTTON)) {
            return {};
        }
        std::array<INPUT, 2> events{};
        for (auto& event : events) {
            event.type = INPUT_MOUSE;
            event.mi.dwExtraInfo = tag;
        }
        events[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
        events[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
        return deliver(events, 0);
    }

    DeliveryResult key(WORD key, unsigned modifiers, ULONG_PTR tag) {
        if (!recover() || backend_.held(key) || (modifiers & ~7u)) {
            return {};
        }
        std::array<INPUT, 8> events{};
        unsigned size = 0;
        const auto add = [&](WORD code, DWORD flags) {
            auto& event = events[size++];
            event.type = INPUT_KEYBOARD;
            event.ki.wVk = code;
            event.ki.dwFlags = flags;
            event.ki.dwExtraInfo = tag;
        };
        constexpr WORD keys[]{VK_SHIFT, VK_CONTROL, VK_MENU};
        for (unsigned i = 0; i < 3; ++i) {
            if ((modifiers & (1u << i)) && !backend_.held(keys[i])) {
                add(keys[i], 0);
            }
        }
        const auto activation = size;
        add(key, 0);
        add(key, KEYEVENTF_KEYUP);
        for (unsigned i = activation; i > 0; --i) {
            add(events[i - 1].ki.wVk, KEYEVENTF_KEYUP);
        }
        return deliver({events.data(), size}, activation);
    }

private:
    DeliveryResult deliver(std::span<INPUT> events, unsigned activation) {
        const auto sent = std::min(static_cast<unsigned>(events.size()), backend_.submit(events));
        for (unsigned i = 0; i < sent; ++i) {
            auto event = events[i];
            const bool up =
                event.type == INPUT_KEYBOARD
                    ? (event.ki.dwFlags & KEYEVENTF_KEYUP) != 0
                    : (event.mi.dwFlags & (MOUSEEVENTF_LEFTUP | MOUSEEVENTF_RIGHTUP)) != 0;
            if (up) {
                --count_;
            } else {
                if (event.type == INPUT_KEYBOARD) {
                    event.ki.dwFlags |= KEYEVENTF_KEYUP;
                } else {
                    event.mi.dwFlags = event.mi.dwFlags == MOUSEEVENTF_LEFTDOWN
                                           ? MOUSEEVENTF_LEFTUP
                                           : MOUSEEVENTF_RIGHTUP;
                }
                releases_[count_++] = event;
            }
        }
        // Only accepted downs create release obligations. Never replay an accepted action.
        recover();
        return {sent, activation};
    }

    DeliveryBackend& backend_;
    std::array<INPUT, 4> releases_{};
    unsigned count_ = 0;
};

class WindowsDelivery final : public DeliveryBackend {
public:
    unsigned submit(std::span<INPUT> events) override {
        return SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
    }

    bool held(WORD key) override {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }
};

inline Delivery& injected_input() {
    static thread_local WindowsDelivery backend;
    static thread_local Delivery delivery(backend);
    return delivery;
}

}
