#pragma once
#include "stored_list_fixture.h"

namespace stored_asset_ref_fixture {
inline std::vector<std::uint8_t> make() {
    using namespace stored_list_fixture;
    auto bytes = stored_list_fixture::make();
    word(bytes, 86, 0x35);
    word(bytes, 118, 0x35);
    word(bytes, 518, 900);
    word(bytes, 522, 6);
    word(bytes, 526, 0xffffffff);
    word(bytes, 530, 0x80000000);
    bytes[534] = 0xff;
    bytes[535] = 2;
    const std::uint8_t name[] = {2, 'a', '\\', 0xff, 0, 'z'};
    std::copy(std::begin(name), std::end(name), bytes.begin() + 900);
    return bytes;
}
}
