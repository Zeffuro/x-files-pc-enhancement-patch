#pragma once

#include "controller_profile.h"

#include <windows.h>
#include <algorithm>
#include <cmath>

namespace enhancements {

class SpringCursor {
public:
    void suspend() {
        active_ = false;
        waiting_ = true;
    }

    bool update(SHORT horizontal, SHORT vertical, const RECT& bounds, POINT& position,
                const controller::Profile& profile = {}) {
        const auto x = controller::axis(horizontal, profile, true);
        const auto y = controller::axis(vertical, profile, true);
        const bool displaced = x != 0 || y != 0;
        if (waiting_) {
            waiting_ = displaced;
            return false;
        }
        if ((!displaced && !active_) || bounds.right <= bounds.left ||
            bounds.bottom <= bounds.top) {
            return false;
        }
        active_ = displaced;
        const auto cx = (bounds.left + bounds.right) / 2;
        const auto cy = (bounds.top + bounds.bottom) / 2;
        position.x =
            std::clamp(cx + static_cast<LONG>(std::lround(x * (bounds.right - bounds.left) / 2)),
                       bounds.left, bounds.right - 1);
        position.y =
            std::clamp(cy - static_cast<LONG>(std::lround(y * (bounds.bottom - bounds.top) / 2)),
                       bounds.top, bounds.bottom - 1);
        return true;
    }

private:
    bool active_ = false;
    bool waiting_ = false;
};

}
