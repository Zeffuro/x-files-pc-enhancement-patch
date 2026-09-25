#include "picture/pict.h"

#include <stdexcept>

namespace {

using Bytes = std::vector<std::uint8_t>;

void word(Bytes& bytes, unsigned value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void rect(Bytes& bytes, int right = 22) {
    for (const auto coordinate : {10, 20, 12, right}) {
        word(bytes, coordinate);
    }
}

Bytes fixture(unsigned depth, unsigned stride, bool sequential, bool monochrome = false) {
    Bytes bytes;
    word(bytes, 0);
    rect(bytes);
    word(bytes, 0x11);
    word(bytes, 0x2ff);
    word(bytes, 1);
    word(bytes, 10);
    rect(bytes, 21);
    word(bytes, 0x98);
    word(bytes, stride | (monochrome ? 0 : 0x8000));
    rect(bytes);
    if (!monochrome) {
        word(bytes, 0);
        word(bytes, 0);
        bytes.resize(bytes.size() + 12);
        word(bytes, 0);
        word(bytes, depth);
        word(bytes, 1);
        word(bytes, depth);
        bytes.resize(bytes.size() + 16);
        word(bytes, sequential ? 0x8000 : 0);
        const unsigned count = depth == 1 ? 2 : 4;
        word(bytes, count - 1);
        for (unsigned entry = 0; entry < count; ++entry) {
            const auto index = sequential ? entry : count - 1 - entry;
            word(bytes, sequential ? 0 : index);
            word(bytes, index == 0 || index == 3 ? 65535 : 0);
            word(bytes, index == 1 || index == 3 ? 65535 : 0);
            word(bytes, index >= 2 ? 65535 : 0);
        }
    }
    rect(bytes);
    rect(bytes);
    word(bytes, 0);
    for (unsigned y = 0; y < 2; ++y) {
        Bytes row(stride, 0);
        if (depth == 8) {
            row[0] = static_cast<std::uint8_t>(2 * y);
            row[1] = static_cast<std::uint8_t>(2 * y + 1);
        } else {
            const auto first = depth == 1 ? y : 2 * y;
            const auto second = depth == 1 ? 1 - y : 2 * y + 1;
            row[0] =
                static_cast<std::uint8_t>((first << (8 - depth)) | (second << (8 - 2 * depth)));
        }
        if (stride >= 8) {
            Bytes packed{128, 1, row[0], row[1]};
            unsigned remaining = stride - 2;
            while (remaining) {
                const auto run = remaining > 128 ? 128 : remaining;
                packed.push_back(static_cast<std::uint8_t>(257 - run));
                packed.push_back(0);
                remaining -= run;
            }
            if (stride > 250) {
                word(bytes, static_cast<unsigned>(packed.size()));
            } else {
                bytes.push_back(static_cast<std::uint8_t>(packed.size()));
            }
            bytes.insert(bytes.end(), packed.begin(), packed.end());
        } else {
            bytes.insert(bytes.end(), row.begin(), row.end());
        }
    }
    if (bytes.size() & 1) {
        bytes.push_back(0);
    }
    word(bytes, 255);
    return bytes;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}

Bytes indexed_fixture() {
    return fixture(8, 8, false);
}

void verify_indexed() {
    const Bytes colors{0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255};
    const Bytes two_colors{0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255};
    for (const auto depth : {1u, 2u, 4u, 8u}) {
        for (const auto stride : {2u, 8u, 250u, 252u}) {
            for (const bool sequential : {false, true}) {
                const auto bytes = fixture(depth, stride, sequential);
                const auto decoded = picture::read(bytes);
                require(decoded.bitmaps.size() == 1 &&
                            decoded.bitmaps[0].pixels == (depth == 1 ? two_colors : colors),
                        "Indexed PICT lost palette indices, bit order, packing or padding.");
                for (std::size_t length = 0; length < bytes.size(); ++length) {
                    bool rejected = false;
                    try {
                        picture::read(std::span(bytes).first(length));
                    } catch (const std::runtime_error&) {
                        rejected = true;
                    }
                    require(rejected, "Truncated indexed PICT was accepted.");
                }
            }
        }
    }
    for (const auto stride : {2u, 8u}) {
        const auto mono = picture::read(fixture(1, stride, false, true));
        const Bytes expected{255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255};
        require(mono.bitmaps[0].pixels == expected, "Monochrome PICT lost black/white order.");
    }
}
