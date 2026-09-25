#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

struct Display {
    std::wstring name;
    long x;
    long y;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t bits;
    std::uint32_t frequency;
    std::uint32_t orientation;

    bool operator==(const Display&) const = default;
};

using Desktop = std::vector<Display>;

Desktop desktop_layout();
void write_desktop(std::ostream& output, const Desktop& desktop);
