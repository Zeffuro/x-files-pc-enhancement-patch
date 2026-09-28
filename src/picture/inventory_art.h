#pragma once

#include <cstdint>
#include <span>
#include <algorithm>

namespace picture {

inline bool is_inventory_art(std::span<const std::uint8_t> packet, unsigned width,
                             unsigned height) {
    struct Signature {
        std::uint64_t hash;
        std::size_t size;
        unsigned width;
        unsigned height;
    };

    // Exact JPEG identities for the shared inventory artwork and its selected states.
    static constexpr Signature signatures[]{
        {0xc8634e30eeca26d4ull, 3833, 30, 40}, {0x6109d5e46aa399fdull, 1230, 30, 40},
        {0x306a532903b83729ull, 2154, 30, 40}, {0x52fa73ad12940353ull, 2321, 30, 40},
        {0xad485947fbe38d39ull, 1635, 30, 40}, {0x61b490576684d8afull, 1472, 30, 40},
        {0x6ffae1718dcc025eull, 2870, 30, 40}, {0x3f0523d1264aa01aull, 3811, 30, 40},
        {0xbcd6e0151fcf6baeull, 2110, 30, 40}, {0x4914d52ccba7af05ull, 2267, 30, 40},
        {0x102cde930a63a1c5ull, 2607, 30, 40}, {0x6fb63e3e0b9eea4bull, 2716, 30, 40},
        {0x6967d78b55c30567ull, 2581, 30, 40}, {0xd77a834a7ca2cd0dull, 2521, 30, 40},
        {0x9088fc56faede11aull, 1992, 22, 40}, {0x914157fb9967a813ull, 2250, 22, 40},
        {0x6528e409c97d83bfull, 1903, 14, 40}, {0xe6ecf4bc56cadc87ull, 2022, 14, 40},
        {0x8ad2f7af393a6fe3ull, 2179, 30, 40}, {0x62bdb67b393ebb62ull, 2261, 30, 40},
        {0xf930102c5af1351dull, 2856, 30, 39}, {0x3edcd52a450d4deaull, 2760, 30, 39},
        {0xadf97d53e18f1bfeull, 2256, 30, 40}, {0x7a35b0b8aaf4708dull, 2360, 30, 40},
        {0x9dc84a1eca519224ull, 4217, 30, 40}, {0x858914c2ef160992ull, 4290, 30, 40},
        {0x89d0e9aa63e5a6a2ull, 1735, 28, 40}, {0xa1be3b43378f3fcbull, 1629, 28, 40},
        {0xe5ac3573df2380b5ull, 1595, 30, 40}, {0xca580dc8bff7ab71ull, 1617, 30, 40},
        {0xb7c348c1a693e976ull, 1393, 30, 40}, {0x17be0a7d1c431053ull, 1652, 30, 40},
        {0xf0ae748ffe54fe21ull, 2688, 30, 40}, {0x02d51af966462afaull, 3052, 30, 40},
        {0xe11fdbbe5c162755ull, 3708, 30, 40}, {0xf71276bb2a79e2feull, 3744, 30, 40},
        {0x56fd9ebd21f9d7ecull, 1673, 30, 40}, {0x184c2dd6e81d59c3ull, 1653, 30, 40},
        {0xb0c29229e021a307ull, 3752, 30, 40}, {0x88e09944653fb080ull, 3726, 30, 40},
        {0xe37cad8f98a728beull, 3653, 30, 40}, {0x62b9346b576e955bull, 3712, 30, 40},
        {0x41cc327dd9af49a1ull, 3232, 30, 40}, {0x4649d8ca70f0aed8ull, 3195, 30, 40},
    };
    if (std::none_of(std::begin(signatures), std::end(signatures), [&](const auto& item) {
            return item.size == packet.size() && item.width == width && item.height == height;
        })) {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : packet) {
        hash = (hash ^ byte) * 1099511628211ull;
    }
    return std::any_of(std::begin(signatures), std::end(signatures), [&](const auto& item) {
        return item.hash == hash && item.size == packet.size() && item.width == width &&
               item.height == height;
    });
}

}
