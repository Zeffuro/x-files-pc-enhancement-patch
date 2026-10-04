#pragma once

#include "game_ui.h"
#include <algorithm>

namespace enhancements::scrolling {

struct Target {
    unsigned resource = 0;
    RECT content{};
    RECT up{};
    RECT down{};
};

Target device_target(const game::ScriptControls& script);

class Wheel {
public:
    int add(int delta) {
        const auto total = static_cast<long long>(remainder_) + delta;
        remainder_ = static_cast<int>(total % WHEEL_DELTA);
        return static_cast<int>(std::clamp(total / WHEEL_DELTA, -8LL, 8LL));
    }

    void reset() {
        remainder_ = 0;
    }

private:
    int remainder_ = 0;
};

bool message(HWND window, UINT message, WPARAM value, LPARAM data);
void reset();
void update(bool available);

}
