#pragma once

#include <array>
#include <cstdint>

namespace enhancements::rumble {

inline constexpr unsigned device_count = 4;
inline constexpr unsigned no_device = device_count;

struct Motors {
    std::uint16_t low = 0;
    std::uint16_t high = 0;
    bool operator==(const Motors&) const = default;
};

struct Effect {
    Motors motors{};
    std::uint32_t duration_ms = 0;
};

class Output {
public:
    virtual ~Output() = default;
    virtual bool set(unsigned player, Motors motors) noexcept = 0;
};

class State {
public:
    void update(Output& output, unsigned player, bool allowed, std::uint64_t now) noexcept;
    void play(Output& output, Effect effect, std::uintptr_t source, std::uint64_t now) noexcept;
    void tick(Output& output, std::uint64_t now) noexcept;
    void cancel(Output& output) noexcept;
    void cancel(Output& output, std::uintptr_t source) noexcept;
    void stop(Output& output) noexcept;

private:
    void silence(Output& output) noexcept;
    unsigned player_ = no_device;
    std::uint64_t lease_ = 0;
    std::uint64_t end_ = 0;
    std::uintptr_t source_ = 0;
    Motors requested_{};
    bool playing_ = false;
    Motors motors_{};
    std::array<bool, device_count> pending_stop_{};
};

}
