#pragma once
#include <cstdint>
#include <string_view>

namespace native_game {
// Script selection values are separate from inventory graphic resource IDs.
constexpr std::wstring_view inventory_selection_name(std::int32_t value) {
    switch (value) {
        case 0:
            return L"None";
        case 9:
            return L"Gun";
        case 3:
            return L"PDA";
        case 4:
            return L"Case files";
        case 5:
            return L"Cattle prod";
        case 6:
            return L"Crowbar";
        case 11:
            return L"Jose Chung";
        case 12:
            return L"Lockpick";
        case 17:
            return L"Photo of NSA car";
        case 20:
            return L"Stiletto";
        case 21:
            return L"Screwdriver";
        case 22:
            return L"Shovel";
        case 24:
            return L"Travel request";
        case 25:
            return L"Wire cutters";
        case 27:
            return L"Paper folder";
        case 29:
            return L"Laptop";
        case 30:
            return L"Videotape";
        case 31:
            return L"Fax log translation";
        default:
            return {};
    }
}
}
