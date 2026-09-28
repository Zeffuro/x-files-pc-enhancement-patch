#pragma once

#include "rumble_state.h"

#include <string_view>

namespace enhancements::rumble {

struct Clip {
    std::wstring_view path;
    Effect effect;
};

Effect clip_effect(std::wstring_view path) noexcept;
Effect short_effect() noexcept;
Effect start_effect(std::wstring_view path, std::int32_t time, std::int32_t prior_rate,
                    std::int32_t rate) noexcept;

class PlaybackCue {
public:
    Effect start(std::wstring_view path, std::int32_t time, std::int32_t prior_rate,
                 std::int32_t rate) noexcept;
    void seek(std::int32_t time) noexcept;

private:
    bool started_ = false;
};

}
