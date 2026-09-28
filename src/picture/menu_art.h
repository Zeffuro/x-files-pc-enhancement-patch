#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

namespace picture {

inline bool is_menu_return_art(std::span<const std::uint8_t> bytes) {
    struct Signature {
        std::uint64_t hash;
        std::size_t size;
    };

    // Full PICT identities of the localized Return buttons.
    static constexpr Signature signatures[]{
        {0x7b29a1d0699b63e1ull, 11816}, {0x3bd901ba85cb086dull, 14418},
        {0x1c3f496216180facull, 9670},  {0xaf71ea2ff22b5190ull, 9288},
        {0x2adf2371b8270caeull, 10600}, {0x913d61e38014f7faull, 6852},
    };
    if (std::none_of(std::begin(signatures), std::end(signatures),
                     [&](const auto& item) { return item.size == bytes.size(); })) {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) {
        hash = (hash ^ byte) * 1099511628211ull;
    }
    return std::any_of(std::begin(signatures), std::end(signatures), [&](const auto& item) {
        return item.hash == hash && item.size == bytes.size();
    });
}

}
