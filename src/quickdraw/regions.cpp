#include "regions.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

namespace {

using namespace quickdraw;

struct RegionStorage {
    NativeRegion shape;
    std::vector<std::int16_t> words;
    Region* pointer = nullptr;
};

thread_local std::unordered_map<RegionHandle, std::unique_ptr<RegionStorage>> regions;

[[noreturn]] void region_error(const char* message) {
    unsupported(Selector::NewRgn, message, 0);
}

NativeRegion rectangle_region(const Rect& bounds) {
    const bool empty = bounds.left >= bounds.right || bounds.top >= bounds.bottom;
    NativeRegion region(empty
                            ? CreateRectRgn(0, 0, 0, 0)
                            : CreateRectRgn(bounds.left, bounds.top, bounds.right, bounds.bottom));
    if (!region) {
        throw std::bad_alloc();
    }
    return region;
}

RegionStorage& storage(RegionHandle handle) {
    const auto found = regions.find(handle);
    if (found == regions.end()) {
        region_error("QuickDraw: unknown region handle");
    }
    return *found->second;
}

std::int16_t coordinate(LONG value) {
    if (value < -32768 || value > 32767) {
        region_error("QuickDraw: region coordinates overflow");
    }
    return static_cast<std::int16_t>(value);
}

std::vector<std::int16_t> encode(HRGN region) {
    RECT bounds{};
    const int kind = GetRgnBox(region, &bounds);
    if (kind == ERROR) {
        region_error("QuickDraw: cannot read region bounds");
    }
    std::vector<std::int16_t> words{10, coordinate(bounds.top), coordinate(bounds.left),
                                    coordinate(bounds.bottom), coordinate(bounds.right)};
    if (kind != COMPLEXREGION) {
        return words;
    }

    const DWORD length = GetRegionData(region, 0, nullptr);
    if (length < sizeof(RGNDATAHEADER)) {
        region_error("QuickDraw: cannot read region data");
    }
    std::vector<std::uint32_t> data((length + 3) / 4);
    auto* native = reinterpret_cast<RGNDATA*>(data.data());
    if (!GetRegionData(region, length, native)) {
        region_error("QuickDraw: cannot copy region data");
    }

    std::map<LONG, std::vector<LONG>> edges;
    const auto* rectangles = reinterpret_cast<const RECT*>(native->Buffer);
    for (DWORD index = 0; index < native->rdh.nCount; ++index) {
        const auto& rectangle = rectangles[index];
        for (const LONG y : {rectangle.top, rectangle.bottom}) {
            edges[y].push_back(rectangle.left);
            edges[y].push_back(rectangle.right);
        }
    }

    // QuickDraw records the horizontal edges that change at each scanline.
    for (auto& [y, points] : edges) {
        std::sort(points.begin(), points.end());
        std::vector<std::int16_t> changes;
        for (std::size_t first = 0; first < points.size();) {
            const auto last = std::upper_bound(points.begin() + first, points.end(), points[first]);
            const auto next = static_cast<std::size_t>(last - points.begin());
            if ((next - first) % 2) {
                if (points[first] == 32767) {
                    region_error("QuickDraw: region edge collides with the terminator");
                }
                changes.push_back(coordinate(points[first]));
            }
            first = next;
        }
        if (changes.empty()) {
            continue;
        }
        if (y == 32767) {
            region_error("QuickDraw: region edge collides with the terminator");
        }
        words.push_back(coordinate(y));
        words.insert(words.end(), changes.begin(), changes.end());
        words.push_back(32767);
    }
    words.push_back(32767);
    if (words.size() > 32766 / sizeof(std::int16_t)) {
        region_error("QuickDraw: region exceeds the supported size");
    }
    words[0] = static_cast<std::int16_t>(words.size() * sizeof(std::int16_t));
    return words;
}

void assign(RegionHandle handle, NativeRegion shape) {
    auto words = encode(shape.get());
    auto& region = storage(handle);
    region.shape = std::move(shape);
    region.words = std::move(words);
    region.pointer = reinterpret_cast<Region*>(region.words.data());
}

RegionHandle __cdecl new_region() {
    try {
        return create_region();
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void __cdecl destroy_region(RegionHandle region) {
    dispose_region(region);
}

void __cdecl copy_region(RegionHandle source, RegionHandle destination) {
    assign(destination, native_region(source));
}

void __cdecl empty_region(RegionHandle region) {
    set_region_rect(region, {});
}

void __cdecl set_rectangle(RegionHandle region, std::int16_t left, std::int16_t top,
                           std::int16_t right, std::int16_t bottom) {
    set_region_rect(region, {top, left, bottom, right});
}

void __cdecl from_rectangle(RegionHandle region, const Rect* bounds) {
    if (!bounds) {
        region_error("RectRgn: missing bounds");
    }
    set_region_rect(region, *bounds);
}

void __cdecl offset_region(RegionHandle region, std::int16_t x, std::int16_t y) {
    auto shape = native_region(region);
    if (OffsetRgn(shape.get(), x, y) == ERROR) {
        region_error("OffsetRgn failed");
    }
    assign(region, std::move(shape));
}

template <int operation>
void __cdecl combine(RegionHandle first, RegionHandle second, RegionHandle destination) {
    const auto left = native_region(first);
    const auto right = native_region(second);
    auto result = rectangle_region({});
    if (CombineRgn(result.get(), left.get(), right.get(), operation) == ERROR) {
        region_error("QuickDraw: region operation failed");
    }
    assign(destination, std::move(result));
}

unsigned char __cdecl rectangle_in_region(const Rect* rectangle, RegionHandle region) {
    if (!rectangle || rectangle->left >= rectangle->right || rectangle->top >= rectangle->bottom) {
        return 0;
    }
    const RECT bounds{rectangle->left, rectangle->top, rectangle->right, rectangle->bottom};
    const auto shape = native_region(region);
    return RectInRegion(shape.get(), &bounds) != FALSE;
}

unsigned char __cdecl equal_regions(RegionHandle first, RegionHandle second) {
    const auto left = native_region(first);
    const auto right = native_region(second);
    return EqualRgn(left.get(), right.get()) != FALSE;
}

unsigned char __cdecl point_in_region(Point point, RegionHandle region) {
    const auto shape = native_region(region);
    return PtInRegion(shape.get(), point.x, point.y) != FALSE;
}

HRGN __cdecl export_region(RegionHandle region) {
    return native_region(region).release();
}

}

namespace quickdraw {

void copy_region_data(RegionHandle source, RegionHandle destination) {
    copy_region(source, destination);
}

RegionHandle create_region(const Rect& bounds) {
    auto region = std::make_unique<RegionStorage>();
    region->shape = rectangle_region(bounds);
    region->words = encode(region->shape.get());
    region->pointer = reinterpret_cast<Region*>(region->words.data());
    RegionHandle handle = &region->pointer;
    regions.emplace(handle, std::move(region));
    return handle;
}

void dispose_region(RegionHandle region) {
    regions.erase(region);
}

void set_region_rect(RegionHandle region, const Rect& bounds) {
    assign(region, rectangle_region(bounds));
}

NativeRegion native_region(RegionHandle handle) {
    auto& region = storage(handle);
    if (region.pointer->size == sizeof(Region)) {
        return rectangle_region(region.pointer->bounds);
    }
    auto result = rectangle_region({});
    if (CombineRgn(result.get(), region.shape.get(), nullptr, RGN_COPY) == ERROR) {
        region_error("QuickDraw: cannot copy region");
    }
    return result;
}

}

Entry region_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::NewRgn, new_region),
        bind_entry(Selector::DisposeRgn, destroy_region),
        bind_entry(Selector::MacCopyRgn, copy_region),
        bind_entry(Selector::SetEmptyRgn, empty_region),
        bind_entry(Selector::MacSetRectRgn, set_rectangle),
        bind_entry(Selector::RectRgn, from_rectangle),
        bind_entry(Selector::MacOffsetRgn, offset_region),
        bind_entry(Selector::SectRgn, combine<RGN_AND>),
        bind_entry(Selector::MacUnionRgn, combine<RGN_OR>),
        bind_entry(Selector::DiffRgn, combine<RGN_DIFF>),
        bind_entry(Selector::RectInRgn, rectangle_in_region),
        bind_entry(Selector::MacEqualRgn, equal_regions),
        bind_entry(Selector::PtInRgn, point_in_region),
        bind_entry(Selector::MacRegionToNativeRegion, export_region),
    };
    return find_entry(selector, entries);
}

void release_regions() {
    regions.clear();
}
