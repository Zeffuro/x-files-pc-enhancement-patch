#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace stored_list_fixture {
inline void word(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(value >> (24 - 8 * i));
    }
}

inline void node(std::vector<std::uint8_t>& bytes, std::size_t at, bool leaf,
                 const std::vector<std::uint32_t>& body) {
    bytes[at] = leaf ? 0xc3 : 0x43;
    bytes[at + 1] = 0x71;
    word(bytes, at + 2, 1);
    bytes[at + 6] = 0;
    bytes[at + 7] = static_cast<std::uint8_t>(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        word(bytes, at + 8 + i * 4, body[i]);
    }
}

inline std::vector<std::uint8_t> make() {
    std::vector<std::uint8_t> bytes(1172);
    word(bytes, 0, 5);
    word(bytes, 8, 32);
    word(bytes, 20, 0x501);
    word(bytes, 24, 0x40000);
    word(bytes, 28, 256);
    node(bytes, 32, true, {});
    bytes[39] = 4;
    const std::uint32_t classes[] = {0x41, 0x42, 0x51, 0x52};
    const std::uint32_t offsets[] = {640, 512, 704, 576};
    const std::uint32_t ids[] = {300, 200, 400, 201};
    for (unsigned i = 0; i < 4; ++i) {
        const auto entry = 40 + i * 43;
        word(bytes, entry + 3, classes[i]);
        bytes[entry + 10] = 1;
        word(bytes, entry + 27, 7);
        word(bytes, entry + 31, 256 + i * 32);
        word(bytes, entry + 35, classes[i]);
        word(bytes, entry + 39, 0x6e756c6c);
        node(bytes, 256 + i * 32, true, {offsets[i], ids[i]});
        bytes[256 + i * 32 + 7] = 1;
        bytes[offsets[i]] = 0xc3;
        word(bytes, offsets[i] + 2, 1);
    }
    bytes[263] = 2;
    word(bytes, 272, 656);
    word(bytes, 276, 301);
    bytes[656] = 0xc3;
    word(bytes, 658, 1);
    for (const auto at : {512u, 576u}) {
        word(bytes, at + 6, at == 512 ? 5 : 3);
        word(bytes, at + 10, at == 512 ? 1024 : 1152);
        word(bytes, at + 14, 0x0b);
        word(bytes, at + 18, 0x6e756c6c);
        word(bytes, at + 22, 1);
        bytes[at + 26] = 0xff;
    }
    word(bytes, 710, 200);
    bytes[714] = 8;
    node(bytes, 1024, true, {300, 0, 300, 0xffffffff, 301});
    node(bytes, 1152, true, {400, 0, 400});
    return bytes;
}
}
