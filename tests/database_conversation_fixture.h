#pragma once
#include "stored_list_fixture.h"

namespace database_conversation_fixture {
inline std::vector<std::uint8_t> make() {
    using namespace stored_list_fixture;
    auto bytes = stored_list_fixture::make();
    const std::uint32_t classes[] = {0x31, 0x2f, 0x2d, 0x32};
    for (unsigned i = 0; i < 4; ++i) {
        word(bytes, 43 + i * 43, classes[i]);
        word(bytes, 75 + i * 43, classes[i]);
    }
    word(bytes, 272, 768);
    bytes[768] = 0xc3;
    word(bytes, 770, 1);
    for (const auto at : {640u, 768u}) {
        word(bytes, at + 6, 200);
        word(bytes, at + 10, 0x80000001);
        word(bytes, at + 14, 0xffffffff);
        word(bytes, at + 18, 0x80000002);
        word(bytes, at + 22, 0x80000003);
        word(bytes, at + 26, 0);
        bytes[at + 30] = 0xfe;
    }
    word(bytes, 518, 900);
    word(bytes, 522, 5);
    word(bytes, 526, 916);
    word(bytes, 530, 3);
    const std::uint8_t title[] = {'A', 0, '\\', 0x80, 0xff};
    std::copy(std::begin(title), std::end(title), bytes.begin() + 900);
    bytes[916] = 'x';
    bytes[917] = 'y';
    bytes[918] = 'z';
    std::fill(bytes.begin() + 710, bytes.begin() + 742, std::uint8_t{0});
    word(bytes, 726, 201);
    word(bytes, 582, 5);
    word(bytes, 586, 1024);
    node(bytes, 1024, true, {300, 0, 300, 0xffffffff, 301});
    return bytes;
}
}
