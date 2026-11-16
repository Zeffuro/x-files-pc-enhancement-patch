#pragma once
#include "stored_action_browser_fixture.h"

namespace conversation_export_fixture {
std::vector<std::uint8_t> make() {
    using namespace stored_list_fixture;
    std::vector<std::uint8_t> bytes(4096);
    word(bytes, 0, 5);
    word(bytes, 8, 32);
    word(bytes, 20, 0x501);
    word(bytes, 24, 0x40000);
    word(bytes, 28, 256);
    node(bytes, 32, true, {});
    bytes[39] = 6;
    const std::uint32_t classes[] = {0x31, 0x2f, 0x2d, 0x32, 0x36, 0x35};
    const std::vector<std::vector<std::uint32_t>> ids{{300, 301}, {200, 201}, {400},
                                                      {201},      {500, 501}, {600, 601, 602}};
    const std::vector<std::vector<std::uint32_t>> offsets{
        {768, 832}, {896, 960}, {1024}, {1088}, {1152, 1216}, {1280, 1344, 1408}};
    for (unsigned i = 0; i < 6; ++i) {
        const auto entry = 40 + i * 43, index = 320 + i * 40;
        word(bytes, entry + 3, classes[i]);
        bytes[entry + 10] = 1;
        word(bytes, entry + 27, 7);
        word(bytes, entry + 31, index);
        word(bytes, entry + 35, classes[i]);
        word(bytes, entry + 39, 0x6e756c6c);
        node(bytes, index, true, {});
        bytes[index + 7] = static_cast<std::uint8_t>(ids[i].size());
        for (std::size_t j = 0; j < ids[i].size(); ++j) {
            word(bytes, index + 8 + 8 * j, offsets[i][j]);
            word(bytes, index + 12 + 8 * j, ids[i][j]);
            bytes[offsets[i][j]] = 0xc3;
            word(bytes, offsets[i][j] + 2, 1);
        }
    }
    word(bytes, 774, 200);
    word(bytes, 794, 500);
    word(bytes, 838, 201);
    word(bytes, 854, 501);
    word(bytes, 846, 501);
    const auto blob = [&](std::size_t at, std::size_t mark, const std::string& value) {
        word(bytes, at + 6, static_cast<std::uint32_t>(mark));
        word(bytes, at + 10, static_cast<std::uint32_t>(value.size()));
        std::copy(value.begin(), value.end(), bytes.begin() + mark);
    };
    blob(896, 2048, "Atoms");
    blob(960, 2080, "Will you file these APBs?");
    word(bytes, 1046, 201);
    const auto listing = [&](std::size_t at, std::size_t mark,
                             const std::vector<std::uint32_t>& members) {
        word(bytes, at + 6, static_cast<std::uint32_t>(members.size()));
        word(bytes, at + 10, static_cast<std::uint32_t>(mark));
        word(bytes, at + 14, 11);
        word(bytes, at + 18, 0x6e756c6c);
        word(bytes, at + 22, 1);
        node(bytes, mark, true, members);
    };
    listing(1088, 2176, {300, 301, 0, 301});
    listing(1152, 2240, {601, 0, 600, 601, 601, 601, 602, 600, 601});
    listing(1216, 2304, {600, 601, 600, 602, 0, 999});
    blob(1280, 2400,
         "\2"
         "12XV\x7f"
         "19794.xmv\1"
         "0");
    blob(1344, 2464,
         "\2"
         "12XV\x7f"
         "19790.xmv\1"
         "0");
    blob(1408, 2528,
         "\2"
         "15../XV\x7f"
         "00000.xmv\1"
         "0");
    return bytes;
}

}
