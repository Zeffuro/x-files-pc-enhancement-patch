#include "caption_surface.h"
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

namespace native_game::caption_surface {
namespace {
struct DeleteRegion {
    void operator()(HRGN value) const {
        DeleteObject(value);
    }
};

using Region = std::unique_ptr<HRGN__, DeleteRegion>;
thread_local std::unordered_map<HGDIOBJ, Region> surfaces;

Region region(const RECT& bounds = {}) {
    return Region(CreateRectRgnIndirect(&bounds));
}

HGDIOBJ bitmap(HDC dc) {
    return dc && GetObjectType(dc) == OBJ_MEMDC ? GetCurrentObject(dc, OBJ_BITMAP) : nullptr;
}

bool device_rect(HDC dc, RECT& bounds) {
    return GetMapMode(dc) == MM_TEXT && LPtoDP(dc, reinterpret_cast<POINT*>(&bounds), 2) != FALSE &&
           bounds.left < bounds.right && bounds.top < bounds.bottom;
}

Region affected(HDC dc, RECT bounds) {
    RECT clip{};
    if (!device_rect(dc, bounds) || GetClipBox(dc, &clip) == ERROR || !device_rect(dc, clip) ||
        !IntersectRect(&bounds, &bounds, &clip)) {
        return region();
    }
    auto result = region(bounds);
    auto mask = region();
    if (result && mask && GetClipRgn(dc, mask.get()) == 1) {
        CombineRgn(result.get(), result.get(), mask.get(), RGN_AND);
    }
    return result;
}

void replace(HGDIOBJ key, HRGN erased, HRGN added) {
    if (!key || !erased) {
        return;
    }
    auto found = surfaces.find(key);
    if (found == surfaces.end()) {
        RECT bounds{};
        if (!added || GetRgnBox(added, &bounds) == NULLREGION || surfaces.size() >= 128) {
            return;
        }
        found = surfaces.emplace(key, region()).first;
    }
    if (!found->second) {
        surfaces.erase(found);
        return;
    }
    CombineRgn(found->second.get(), found->second.get(), erased, RGN_DIFF);
    if (added) {
        CombineRgn(found->second.get(), found->second.get(), added, RGN_OR);
    }
    RECT bounds{};
    const auto bytes = GetRegionData(found->second.get(), 0, nullptr);
    if (!bytes || bytes > 65536 || GetRgnBox(found->second.get(), &bounds) == NULLREGION) {
        surfaces.erase(found);
    }
}
}

void paint(HDC dc, const RECT& overwritten, std::optional<RECT> caption) noexcept try {
    const auto key = bitmap(dc);
    if (!key || (!caption && !surfaces.contains(key))) {
        return;
    }
    auto erased = affected(dc, overwritten);
    auto added = caption ? affected(dc, *caption) : Region{};
    replace(key, erased.get(), added.get());
} catch (...) {

    clear();
}

void copy(HDC destination, const RECT& to, HDC source, const RECT& from, DWORD operation) noexcept
    try {
    const auto key = bitmap(destination);
    if (!key) {
        return;
    }
    const auto found = surfaces.find(bitmap(source));
    if (found == surfaces.end() && !surfaces.contains(key)) {
        return;
    }
    auto erased = affected(destination, to);
    Region added;
    RECT input = from;
    RECT output = to;
    if (found != surfaces.end() && operation == SRCCOPY && device_rect(source, input) &&
        device_rect(destination, output)) {
        auto clipped = region(input);
        if (clipped) {
            CombineRgn(clipped.get(), clipped.get(), found->second.get(), RGN_AND);
            const auto size = GetRegionData(clipped.get(), 0, nullptr);
            if (size && size <= 65536) {
                std::vector<DWORD> data((size + sizeof(DWORD) - 1) / sizeof(DWORD));
                auto* contents = reinterpret_cast<RGNDATA*>(data.data());
                if (GetRegionData(clipped.get(), size, contents)) {
                    const float x =
                        static_cast<float>(output.right - output.left) / (input.right - input.left);
                    const float y =
                        static_cast<float>(output.bottom - output.top) / (input.bottom - input.top);
                    const XFORM transform{
                        x, 0, 0, y, output.left - input.left * x, output.top - input.top * y};
                    added.reset(ExtCreateRegion(&transform, size, contents));
                    if (added && erased) {
                        CombineRgn(added.get(), added.get(), erased.get(), RGN_AND);
                    }
                }
            }
        }
    }
    replace(key, erased.get(), added.get());
} catch (...) {

    clear();
}

void forget(HGDIOBJ value) {
    surfaces.erase(value);
}

void clear() {
    surfaces.clear();
}

bool any() {
    return !surfaces.empty();
}

int credit_offset(HDC dc, const RECT& credit, int top) noexcept try {
    const auto found = surfaces.find(bitmap(dc));
    auto bounds = credit;
    if (found == surfaces.end() || !device_rect(dc, bounds)) {
        return 0;
    }
    const auto size = GetRegionData(found->second.get(), 0, nullptr);
    if (!size || size > 65536) {
        return 0;
    }
    std::vector<DWORD> data((size + sizeof(DWORD) - 1) / sizeof(DWORD));
    auto* contents = reinterpret_cast<RGNDATA*>(data.data());
    if (!GetRegionData(found->second.get(), size, contents)) {
        return 0;
    }
    const auto* rectangles = reinterpret_cast<const RECT*>(contents->Buffer);
    POINT ceiling{0, top};
    LPtoDP(dc, &ceiling, 1);
    const auto original = bounds.top;
    for (DWORD pass = 0; pass <= contents->rdh.nCount; ++pass) {
        int offset = 0;
        for (DWORD i = 0; i < contents->rdh.nCount; ++i) {
            RECT overlap{};
            if (IntersectRect(&overlap, &bounds, &rectangles[i])) {
                offset = std::min<LONG>(offset, rectangles[i].top - bounds.bottom - 4);
            }
        }
        offset = std::max<LONG>(offset, ceiling.y - bounds.top);
        if (!offset) {
            break;
        }
        OffsetRect(&bounds, 0, offset);
    }
    return bounds.top - original;
} catch (...) {

    clear();
    return 0;
}
}
