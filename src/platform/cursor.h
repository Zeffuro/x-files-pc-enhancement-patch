#pragma once

#include <windows.h>
#include <stdexcept>

struct CursorClip {
    long left;
    long top;
    long right;
    long bottom;

    bool operator==(const CursorClip&) const = default;
};

inline CursorClip cursor_clip(decltype(&GetClipCursor) query = GetClipCursor) {
    RECT rectangle;
    if (!query(&rectangle)) {
        throw std::runtime_error("Cannot read cursor confinement.");
    }
    return {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
}
