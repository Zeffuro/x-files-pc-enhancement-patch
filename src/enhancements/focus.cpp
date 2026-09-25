#include "input_source.h"
#include "focus.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace enhancements {

std::vector<RECT> main_menu_targets(bool can_save) {
    std::vector<RECT> items{
        {472, 75, 639, 120},  {472, 125, 639, 170}, {472, 175, 639, 220}, {472, 225, 639, 270},
        {472, 275, 639, 320}, {472, 325, 639, 370}, {472, 375, 639, 430},
    };
    if (!can_save) {
        items.erase(items.begin() + 2);
    }
    return items;
}

int directional_target(std::span<const RECT> targets, POINT cursor, int horizontal, int vertical) {
    if (targets.empty() || (!horizontal && !vertical)) {
        return -1;
    }
    int selected = -1;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        if (PtInRect(&targets[index], cursor)) {
            selected = static_cast<int>(index);
            cursor = {(targets[index].left + targets[index].right) / 2,
                      (targets[index].top + targets[index].bottom) / 2};
            break;
        }
    }
    if (selected < 0) {
        return horizontal < 0 || vertical < 0 ? static_cast<int>(targets.size()) - 1 : 0;
    }
    for (const bool wrap : {false, true}) {
        int next = -1;
        long best = std::numeric_limits<long>::max();
        for (std::size_t index = 0; index < targets.size(); ++index) {
            if (static_cast<int>(index) == selected) {
                continue;
            }
            const auto& target = targets[index];
            const auto x = (target.left + target.right) / 2 - cursor.x;
            const auto y = (target.top + target.bottom) / 2 - cursor.y;
            const auto along = horizontal ? x * horizontal : y * vertical;
            const auto across = std::abs(horizontal ? y : x);
            if ((!wrap && along <= 0) || (wrap && along >= 0)) {
                continue;
            }
            const auto score = along + across * 8;
            if (score < best) {
                best = score;
                next = static_cast<int>(index);
            }
        }
        if (next >= 0) {
            return next;
        }
    }
    return selected;
}

int hotspot_target(std::span<const RECT> targets, POINT cursor, int direction) {
    if (!direction || targets.empty()) {
        return -1;
    }
    int current = -1;
    long current_area = std::numeric_limits<long>::max();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& item = targets[i];
        const auto area = (item.right - item.left) * (item.bottom - item.top);
        if (PtInRect(&item, cursor) && area < current_area) {
            current = static_cast<int>(i);
            current_area = area;
        }
    }
    if (current >= 0) {
        const auto& item = targets[current];
        cursor = {(item.left + item.right) / 2, (item.top + item.bottom) / 2};
    }
    for (const bool wrap : {false, true}) {
        int next = -1;
        long long best = std::numeric_limits<long long>::max();
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (static_cast<int>(i) == current) {
                continue;
            }
            const auto& item = targets[i];
            const long long x = (item.left + item.right) / 2;
            const long long dx = x - cursor.x;
            const long long dy = (item.top + item.bottom) / 2 - cursor.y;
            if (!wrap && dx * direction <= 0) {
                continue;
            }
            const auto score = wrap ? x * direction * 1000000 + dy * dy : dx * dx + dy * dy;
            if (score < best) {
                best = score;
                next = static_cast<int>(i);
            }
        }
        if (next >= 0) {
            return next;
        }
    }
    return current;
}

bool exposed_target(const RECT& bounds, std::span<const RECT> occluders, RECT& target) {
    std::vector<RECT> regions{bounds};
    for (const auto& occluder : occluders) {
        std::vector<RECT> remaining;
        for (const auto& region : regions) {
            RECT overlap{};
            if (!IntersectRect(&overlap, &region, &occluder)) {
                remaining.push_back(region);
                continue;
            }
            const RECT pieces[]{{region.left, region.top, overlap.left, region.bottom},
                                {overlap.right, region.top, region.right, region.bottom},
                                {overlap.left, region.top, overlap.right, overlap.top},
                                {overlap.left, overlap.bottom, overlap.right, region.bottom}};
            for (const auto& piece : pieces) {
                if (piece.right > piece.left && piece.bottom > piece.top) {
                    remaining.push_back(piece);
                }
            }
        }
        regions = std::move(remaining);
        if (regions.empty()) {
            return false;
        }
    }
    long largest = 0;
    for (const auto& region : regions) {
        const auto area = (region.right - region.left) * (region.bottom - region.top);
        if (area > largest) {
            largest = area;
            target = region;
        }
    }
    return largest > 0;
}

bool point_controller(HWND window, const RECT& target, bool activate, bool right) {
    POINT cursor{(target.left + target.right) / 2, (target.top + target.bottom) / 2};
    if (!ClientToScreen(window, &cursor) || !move_controller_pointer(cursor.x, cursor.y)) {
        return false;
    }
    if (activate) {
        INPUT events[2]{};
        events[0].type = events[1].type = INPUT_MOUSE;
        events[0].mi.dwExtraInfo = events[1].mi.dwExtraInfo = controller_event;
        events[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
        events[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
        return SendInput(2, events, sizeof(INPUT)) == 2;
    }
    return true;
}

}
