#include "internal.h"
#include <bit>

namespace devtools::recipes::detail {
std::string digest(std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    std::array<std::uint32_t, 8> hash{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::size_t total = ((bytes.size() + 9 + 63) / 64) * 64;
    for (std::size_t start = 0; start < total; start += 64) {
        std::array<std::uint32_t, 64> words{};
        for (unsigned i = 0; i < 64; ++i) {
            auto at = start + i;
            unsigned byte = at < bytes.size() ? bytes[at] : at == bytes.size() ? 0x80 : 0;
            if (at >= total - 8) {
                byte = static_cast<unsigned>((std::uint64_t(bytes.size()) * 8) >>
                                             (8 * (total - at - 1))) &
                       255;
            }
            words[i / 4] |= byte << (24 - 8 * (i % 4));
        }
        for (unsigned i = 16; i < 64; ++i) {
            auto a = words[i - 15], b = words[i - 2];
            auto s0 = std::rotr(a, 7) ^ std::rotr(a, 18) ^ (a >> 3),
                 s1 = std::rotr(b, 17) ^ std::rotr(b, 19) ^ (b >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        auto state = hash;
        for (unsigned i = 0; i < 64; ++i) {
            auto a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5],
                 g = state[6], h = state[7];
            auto t1 = h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
                      ((e & f) ^ ((~e) & g)) + constants[i] + words[i];
            auto t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) +
                      ((a & b) ^ (a & c) ^ (b & c));
            state = {t1 + t2, a, b, c, d + t1, e, f, g};
        }
        for (unsigned i = 0; i < 8; ++i) {
            hash[i] += state[i];
        }
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (auto word : hash) {
        for (unsigned i = 0; i < 8; ++i) {
            result += hex[(word >> (28 - 4 * i)) & 15];
        }
    }
    return result;
}
}
