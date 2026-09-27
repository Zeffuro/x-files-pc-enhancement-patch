#pragma once
#include "quickdraw/types.h"
#include "settings.h"
#include <windows.h>

namespace playback {
struct CaptionLayout {
    quickdraw::Rect image{};
    quickdraw::Rect caption{};
    bool below = false;
};

inline CaptionLayout layout_captions(const quickdraw::Rect& box, const quickdraw::Rect&,
                                     const RECT&, const CaptionStyle&) {
    // Letterbox captions need final screen coordinates, not the movie's offscreen port.
    return {box, box, false};
}
}
