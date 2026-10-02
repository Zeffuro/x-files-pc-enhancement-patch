#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace devtools {
inline bool database_copy(std::uintptr_t address, void* value, std::size_t size) {
    if (!address || !size || size > UINT32_MAX || address > UINT32_MAX - size) {
        return false;
    }
    auto cursor = address;
    auto remaining = size;
    while (remaining) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) ||
            region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD)) {
            return false;
        }
        switch (region.Protect & 0xff) {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                break;
            default:
                return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (cursor < base || cursor - base >= region.RegionSize) {
            return false;
        }
        const auto available = region.RegionSize - (cursor - base);
        if (remaining <= available) {
            break;
        }
        remaining -= available;
        cursor += available;
    }
    SIZE_T count = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), value,
                             size, &count) &&
           count == size;
}
}
