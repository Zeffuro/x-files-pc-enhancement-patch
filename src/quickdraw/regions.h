#pragma once

#include "runtime.h"
#include "types.h"

#include <windows.h>
#include <memory>
#include <type_traits>

namespace quickdraw {

using RegionHandle = Region**;

struct RegionDeleter {
    void operator()(HRGN region) const {
        DeleteObject(region);
    }
};

using NativeRegion = std::unique_ptr<std::remove_pointer_t<HRGN>, RegionDeleter>;

RegionHandle create_region(const Rect& bounds = {});
void dispose_region(RegionHandle region);
void set_region_rect(RegionHandle region, const Rect& bounds);
void copy_region_data(RegionHandle source, RegionHandle destination);
NativeRegion native_region(RegionHandle region);

}

Entry region_entry(Selector selector);
void release_regions();
