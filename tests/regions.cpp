#include "dispatch.h"
#include "quickdraw/types.h"

#include <bit>
#include <iostream>
#include <vector>

namespace {

using namespace test;
using namespace quickdraw;
using RegionHandle = Region**;

RegionHandle rectangle(const Rect& bounds) {
    const auto region = reinterpret_cast<RegionHandle>(invoke(Selector::NewRgn));
    require(region && *region, "NewRgn failed.");
    invoke(Selector::RectRgn, address(region), address(&bounds));
    return region;
}

bool contains(RegionHandle region, std::int16_t x, std::int16_t y) {
    return (invoke(Selector::PtInRgn, std::bit_cast<std::uint32_t>(Point{y, x}), address(region)) &
            0xff) != 0;
}

void verify_regions() {
    const auto outer = rectangle({0, 0, 10, 10});
    const auto inner = rectangle({2, 2, 8, 8});
    const auto result = rectangle({});
    require((*outer)->size == 10 && (*outer)->bounds.right == 10,
            "Rectangular region layout is wrong.");
    require(contains(outer, 0, 0) && !contains(outer, 10, 0) && !contains(outer, 0, 10),
            "Point ABI or exclusive rectangle edges are wrong.");

    invoke(Selector::DiffRgn, address(outer), address(inner), address(result));
    require(contains(result, 1, 5) && !contains(result, 5, 5), "Region subtraction lost its hole.");
    const std::vector<std::int16_t> expected{44,    0, 0, 10, 10,    0,  0, 10, 32767, 2,    2, 8,
                                             32767, 8, 2, 8,  32767, 10, 0, 10, 32767, 32767};
    const auto* words = reinterpret_cast<const std::int16_t*>(*result);
    require((*result)->size == static_cast<std::int16_t>(expected.size() * 2) &&
                std::equal(expected.begin(), expected.end(), words),
            "Complex region scanlines do not match the QuickDraw representation.");

    const Rect crossing{4, 0, 6, 4};
    const Rect hole{3, 3, 7, 7};
    require((invoke(Selector::RectInRgn, address(&crossing), address(result)) & 0xff) == 1 &&
                (invoke(Selector::RectInRgn, address(&hole), address(result)) & 0xff) == 0,
            "RectInRgn must test intersection, including holes.");

    invoke(Selector::MacCopyRgn, address(result), address(outer));
    invoke(Selector::MacOffsetRgn, address(result), 20, 30);
    require(contains(result, 21, 35) && !contains(result, 25, 35) && contains(outer, 1, 5),
            "OffsetRgn changed a copied region or filled its hole.");
    invoke(Selector::MacUnionRgn, address(outer), address(inner), address(outer));
    require(contains(outer, 5, 5) && (*outer)->size == 10,
            "Aliased region union did not restore a rectangle.");
    invoke(Selector::SectRgn, address(outer), address(inner), address(outer));
    require((invoke(Selector::MacEqualRgn, address(outer), address(inner)) & 0xff) == 1,
            "Aliased region intersection differs from the inner rectangle.");
    invoke(Selector::SetEmptyRgn, address(outer));
    require(!contains(outer, 5, 5) && (*outer)->bounds.right == 0,
            "SetEmptyRgn left a nonempty region.");
    invoke(Selector::DisposeRgn, address(outer));
    invoke(Selector::DisposeRgn, address(inner));
    invoke(Selector::DisposeRgn, address(result));
}

}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto library = LoadLibraryW(argv[1]);
    if (!library) {
        return 1;
    }
    dispatcher = GetProcAddress(library, "theQuickTimeDispatcher");
    if (!dispatcher) {
        FreeLibrary(library);
        return 1;
    }
    int result = 0;
    try {
        invoke(Selector::QTMLInitInternals, 2);
        verify_regions();
        const auto source = rectangle({3, 7, 12, 18});
        const auto native =
            reinterpret_cast<HRGN>(invoke(Selector::MacRegionToNativeRegion, address(source)));
        invoke(Selector::DisposeRgn, address(source));
        require(native && PtInRegion(native, 7, 3) && !PtInRegion(native, 18, 12),
                "Exported region has incorrect geometry or lifetime.");
        require(DeleteObject(native) != FALSE, "Exported region cannot be released by Windows.");
        const auto objects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 100; ++i) {
            verify_regions();
        }
        rectangle({0, 0, 10, 10});
        invoke(Selector::QTMLTermInternals);
        require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == objects,
                "Region disposal or termination leaked GDI objects.");
        std::cout << "Region ABI, scanlines, geometry and cleanup passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
        invoke(Selector::QTMLTermInternals);
    }
    FreeLibrary(library);
    return result;
}
