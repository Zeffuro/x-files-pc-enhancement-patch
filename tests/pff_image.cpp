#include "devtools/database/pff_image.h"
#include "picture/pict.h"
#include "picture/opcodes.h"
#include "game/assets/pff.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using Bytes = std::vector<std::uint8_t>;
using Rect = quickdraw::Rect;
using Pixel = std::array<std::uint8_t, 4>;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class Operation> void reject(Operation operation, const char* message) {
    bool failed = false;
    try {
        operation();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, message);
}

void word(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void dword(Bytes& bytes, std::uint32_t value) {
    word(bytes, static_cast<std::uint16_t>(value >> 16));
    word(bytes, static_cast<std::uint16_t>(value));
}

void rect(Bytes& bytes, Rect value) {
    for (const auto coordinate : {value.top, value.left, value.bottom, value.right}) {
        word(bytes, static_cast<std::uint16_t>(coordinate));
    }
}

Bytes header(Rect bounds) {
    Bytes bytes;
    word(bytes, 0);
    rect(bytes, bounds);
    word(bytes, 0x0011);
    word(bytes, 0x02ff);
    return bytes;
}

void bitmap(Bytes& bytes, Rect bounds, Rect source, Rect destination, Rect clip, std::uint16_t mode,
            unsigned stride, std::span<const Pixel> colors) {
    word(bytes, 0x0001);
    word(bytes, 10);
    rect(bytes, clip);
    word(bytes, 0x009a);
    dword(bytes, 0xff);
    word(bytes, 0x8000 | static_cast<std::uint16_t>(stride));
    rect(bytes, bounds);
    word(bytes, 0);
    word(bytes, 1);
    bytes.resize(bytes.size() + 12);
    word(bytes, 16);
    word(bytes, 32);
    word(bytes, 3);
    word(bytes, 8);
    bytes.resize(bytes.size() + 12);
    rect(bytes, source);
    rect(bytes, destination);
    word(bytes, mode);
    const auto width = unsigned(bounds.right - bounds.left);
    const auto height = unsigned(bounds.bottom - bounds.top);
    require(colors.size() == std::size_t(width) * height, "Fixture pixel count invalid");
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto& pixel = colors[std::size_t(y) * width + x];
            bytes.insert(bytes.end(), {pixel[3], pixel[2], pixel[1], pixel[0]});
        }
        bytes.resize(bytes.size() + stride - width * 4, 0xee);
    }
}

Pixel pixel(const media::Frame& image, unsigned x, unsigned y) {
    Pixel result{};
    std::copy_n(image.pixels.begin() + (std::size_t(y) * image.width + x) * 4, 4, result.begin());
    return result;
}

void solid(Bytes& bytes, Rect destination, Rect clip) {
    word(bytes, 1);
    word(bytes, 10);
    rect(bytes, clip);
    word(bytes, 9);
    bytes.resize(bytes.size() + 8, 255);
    word(bytes, 0x31);
    rect(bytes, destination);
}

void placeholders() {
    const Pixel black{0, 0, 0, 255}, red{5, 11, 237, 255}, blue{229, 17, 23, 255};
    const Rect canvas{-2, -3, 2, 1};
    auto bytes = header(canvas);
    const std::array<Pixel, 1> backdrop{red}, foreground{blue};
    bitmap(bytes, {0, 0, 1, 1}, {0, 0, 1, 1}, canvas, canvas, 0, 4, backdrop);
    solid(bytes, {-3, -4, 1, 0}, {-1, -2, 1, 0});
    bitmap(bytes, {0, 0, 1, 1}, {0, 0, 1, 1}, {-1, -2, 0, -1}, canvas, 0, 4, foreground);
    word(bytes, 0xff);
    const auto image = devtools::decode_pff_image(bytes);
    require(pixel(image, 0, 0) == red && pixel(image, 1, 1) == blue &&
                pixel(image, 2, 1) == black && pixel(image, 1, 2) == black &&
                pixel(image, 3, 2) == red,
            "Solid rectangle signed placement, clip or paint order changed");
    auto simple = header({0, 0, 1, 1});
    solid(simple, {0, 0, 1, 1}, {0, 0, 1, 1});
    word(simple, 0xff);
    require(pixel(devtools::decode_pff_image(simple), 0, 0) == black,
            "Solid placeholder was not native black");
    reject([&] { picture::read(simple); }, "Default native parser enabled vector rendering");
    reject([&] { picture::read(simple, {0, 4, true}); }, "Solid bitmap count limit ignored");
    reject([&] { picture::read(simple, {1, 3, true}); }, "Solid storage budget ignored");
    require(picture::read(simple, {1, 4, true}).bitmaps.size() == 1,
            "Solid exact storage boundary rejected");
    for (std::size_t length = 0; length < simple.size(); ++length) {
        reject([&] { devtools::decode_pff_image(std::span(simple).first(length)); },
               "Truncated vector placeholder accepted");
    }
    simple[28] = 0x7f;
    reject([&] { devtools::decode_pff_image(simple); }, "Non-solid pen pattern was guessed");
}

void geometry() {
    const Pixel red{3, 7, 251, 255}, green{11, 241, 17, 255}, blue{231, 23, 29, 255};
    const Pixel white{255, 255, 255, 255};
    const Rect canvas{10, 20, 14, 26};
    auto bytes = header(canvas);
    const std::array colors{red, green, blue, white};
    bitmap(bytes, {30, 40, 32, 42}, {30, 40, 32, 42}, {10, 20, 14, 24}, {11, 21, 14, 23}, 64, 12,
           colors);
    word(bytes, 0xff);
    const auto image = devtools::decode_pff_image(bytes);
    require(image.width == 6 && image.height == 4, "Canvas dimensions or origin lost");
    require(pixel(image, 0, 1) == white && pixel(image, 1, 0) == white &&
                pixel(image, 1, 1) == red && pixel(image, 2, 1) == green &&
                pixel(image, 1, 2) == blue && pixel(image, 2, 2) == white &&
                pixel(image, 3, 1) == white,
            "Nearest placement, clip, stride or original RGB values changed");
    reject([&] { picture::read(bytes, {0, 64}); }, "Bitmap count limit ignored");
    reject([&] { picture::read(bytes, {1, 23}); }, "Bitmap allocation limit ignored");
    require(picture::read(bytes, {1, 24}).bitmaps.size() == 1,
            "Exact bitmap allocation boundary rejected");
    auto cropped = header({0, 0, 1, 3});
    const std::array row{red, green, blue};
    bitmap(cropped, {0, 0, 1, 3}, {0, 1, 1, 3}, {0, -1, 1, 3}, {0, 0, 1, 3}, 0, 12, row);
    word(cropped, 0xff);
    const auto crop = devtools::decode_pff_image(cropped);
    require(pixel(crop, 0, 0) == green && pixel(crop, 1, 0) == blue && pixel(crop, 2, 0) == blue,
            "Source crop or off-canvas destination mapping changed");
    auto layered = header({0, 0, 1, 1});
    const std::array<Pixel, 1> first{red}, second{blue};
    for (const auto& layer : {first, second}) {
        bitmap(layered, {0, 0, 1, 1}, {0, 0, 1, 1}, {0, 0, 1, 1}, {0, 0, 1, 1}, 0, 4, layer);
    }
    word(layered, 0xff);
    require(pixel(devtools::decode_pff_image(layered), 0, 0) == blue, "Layer order lost");
    reject([&] { picture::read(layered, {1, 8}); }, "Aggregate bitmap count ignored");
    reject([&] { picture::read(layered, {2, 7}); }, "Aggregate bitmap bytes ignored");
}

void roundtrip() {
    media::Frame image{3, 2, {0,   1,   2,   255, 10, 20, 30, 255, 250, 240, 230, 255,
                              127, 128, 129, 255, 8,  9,  11, 255, 255, 0,   77,  255}};
    const auto encoded = devtools::encode_pff_image(image);
    const auto decoded = devtools::decode_pff_image(encoded);
    require(decoded.width == image.width && decoded.height == image.height &&
                decoded.pixels == image.pixels,
            "Uncompressed PICT roundtrip altered RGB colors or geometry");
    for (std::size_t length = 0; length < encoded.size(); ++length) {
        reject([&] { devtools::decode_pff_image(std::span(encoded).first(length)); },
               "Truncated PICT accepted");
    }
    auto unsupported = encoded;
    unsupported[120] = 0;
    unsupported[121] = 36;
    reject([&] { devtools::decode_pff_image(unsupported); }, "Transparent transfer was guessed");
    image.pixels[3] = 0;
    reject([&] { devtools::encode_pff_image(image); }, "Transparent import accepted");
    image.pixels[3] = 255;
    image.pixels.pop_back();
    reject([&] { devtools::encode_pff_image(image); }, "Short import buffer accepted");
    for (const auto dimensions : {std::pair{0u, 1u}, std::pair{4096u, 1u},
                                  std::pair{32767u, 32767u}, std::pair{UINT32_MAX, UINT32_MAX}}) {
        reject([&] { devtools::encode_pff_image({dimensions.first, dimensions.second, {}}); },
               "Invalid or overflowing import dimensions accepted");
    }
    auto empty = header({0, 0, 1, 1});
    word(empty, 0xff);
    reject([&] { devtools::decode_pff_image(empty); }, "Empty picture accepted as image");
}

void compressed() {
    Bytes body;
    word(body, 0);
    for (const auto value : {65536u, 0u, 0u, 0u, 65536u, 0u, 2u * 65536, 3u * 65536, 0x40000000u}) {
        dword(body, value);
    }
    dword(body, 0);
    rect(body, {});
    word(body, 64);
    rect(body, {0, 0, 4, 4});
    dword(body, 0);
    dword(body, 0);
    dword(body, 86);
    body.insert(body.end(), {'r', 'p', 'z', 'a'});
    body.resize(body.size() + 24);
    word(body, 4);
    word(body, 4);
    dword(body, 72 << 16);
    dword(body, 72 << 16);
    dword(body, 7);
    word(body, 1);
    body.resize(body.size() + 32);
    word(body, 24);
    word(body, 65535);
    body.insert(body.end(), {0xe1, 0, 0, 7, 0xa0, 0x7c, 0});
    auto bytes = header({0, 0, 8, 8});
    word(bytes, 1);
    word(bytes, 10);
    rect(bytes, {4, 3, 6, 5});
    word(bytes, 0x8200);
    dword(bytes, static_cast<std::uint32_t>(body.size()));
    bytes.insert(bytes.end(), body.begin(), body.end());
    if (bytes.size() & 1) {
        bytes.push_back(0);
    }
    word(bytes, 0xff);
    const auto decoded = devtools::decode_pff_image(bytes);
    const Pixel red{0, 0, 255, 255}, white{255, 255, 255, 255};
    require(pixel(decoded, 3, 4) == red && pixel(decoded, 4, 5) == red &&
                pixel(decoded, 2, 4) == white && pixel(decoded, 3, 3) == white &&
                pixel(decoded, 5, 4) == white,
            "Compressed codec byte order, original color, translation or clipping changed");
    reject([&] { picture::read(bytes, {1, 6}); }, "Compressed storage budget ignored");
}

std::size_t audit(const std::filesystem::path& path) {
    std::wstring error;
    const auto archive = game_assets::PffArchive::load(path, &error);
    if (!archive) {
        std::wcerr << path.wstring() << L": " << error << L'\n';
        return 1;
    }
    std::size_t decoded = 0, failed = 0;
    for (std::size_t index = 0; index < archive->entries().size(); ++index) {
        try {
            const auto frame = devtools::decode_pff_image(archive->entry(index));
            if (path.filename() == "X.PFF" &&
                (index == 133 || index == 317 || index == 318 || index == 319)) {
                require(frame.width == 1 && frame.height == 1 &&
                            pixel(frame, 0, 0) == Pixel{0, 0, 0, 255},
                        "Installed vector placeholder was not black 1x1");
            }
            const auto roundtrip = devtools::decode_pff_image(devtools::encode_pff_image(frame));
            require(frame.width == roundtrip.width && frame.height == roundtrip.height &&
                        frame.pixels == roundtrip.pixels,
                    "Installed PICT RGB roundtrip changed pixels");
            ++decoded;
        } catch (const std::exception& failure) {
            std::cout << path << " entry " << index << ": " << failure.what() << '\n';
            ++failed;
        }
    }
    std::cout << path << ": " << decoded << " decoded and RGB-roundtripped, " << failed
              << " explicit failures, " << archive->entries().size() << " entries visited\n";
    return failed;
}
}

int main(int argc, char** argv) {
    try {
        geometry();
        roundtrip();
        compressed();
        placeholders();
        std::size_t failures = 0;
        for (int argument = 1; argument < argc; ++argument) {
            failures += audit(argv[argument]);
        }
        require(!failures, "Installed image audit had explicit failures");
        std::cout << "PFF image placement, clipping, color, encoding and limits passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
