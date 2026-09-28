#pragma once

#include <array>
#include <cstdint>

namespace enhancements::input {

inline constexpr unsigned max_devices = 4;
inline constexpr unsigned no_device = max_devices;
inline constexpr int left_deadzone = 7849;
inline constexpr std::uint8_t trigger_threshold = 30;

namespace button {
inline constexpr std::uint16_t activate = 1 << 0;
inline constexpr std::uint16_t inventory = 1 << 1;
inline constexpr std::uint16_t examine = 1 << 2;
inline constexpr std::uint16_t back = 1 << 3;
inline constexpr std::uint16_t skip = 1 << 4;
inline constexpr std::uint16_t menu = 1 << 5;
inline constexpr std::uint16_t previous = 1 << 6;
inline constexpr std::uint16_t next = 1 << 7;
inline constexpr std::uint16_t evidence = 1 << 8;
inline constexpr std::uint16_t up = 1 << 9;
inline constexpr std::uint16_t down = 1 << 10;
inline constexpr std::uint16_t left = 1 << 11;
inline constexpr std::uint16_t right = 1 << 12;
inline constexpr std::uint16_t speed = 1 << 13;
}

struct Sample {
    std::uint16_t buttons = 0;
    std::uint8_t left_trigger = 0;
    std::uint8_t right_trigger = 0;
    std::int16_t left_x = 0;
    std::int16_t left_y = 0;
};

struct Frame {
    Sample sample{};
    std::uint16_t pressed = 0;
    bool aim_pressed = false;
    bool connected = false;
    bool device_changed = false;
    unsigned player = no_device;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual bool read(unsigned player, Sample& sample) = 0;
};

class Selection {
public:
    Frame poll(Backend& backend, bool enabled);

private:
    std::array<Sample, max_devices> previous_{};
    std::array<bool, max_devices> seen_neutral_{};
    unsigned player_ = no_device;
    std::uint16_t previous_buttons_ = 0;
    bool previous_aim_ = false;
    bool armed_ = false;
    std::uint16_t blocked_buttons_ = 0;
    bool blocked_left_trigger_ = false;
    bool blocked_right_trigger_ = false;
    bool blocked_left_x_ = false;
    bool blocked_left_y_ = false;
};

Frame poll(bool enabled);

}
