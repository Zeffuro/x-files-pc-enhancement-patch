#pragma once
#include "stored_list_fixture.h"

namespace stored_asset_list_fixture {
inline std::vector<std::uint8_t> make() {
    using namespace stored_list_fixture;
    auto bytes = stored_list_fixture::make();
    word(bytes, 86, 0x36);
    word(bytes, 118, 0x36);
    return bytes;
}
}
