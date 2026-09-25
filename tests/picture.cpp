#include "dispatch.h"
#include "picture/pict.h"
#include "picture/opcodes.h"

#include <cstring>
#include <fstream>
#include <iostream>

std::vector<std::uint8_t> indexed_fixture();
void verify_indexed();

namespace {

using namespace test;
using namespace quickdraw;
using picture::Opcode;
using Bytes = std::vector<std::uint8_t>;

void word(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void opcode(Bytes& bytes, Opcode value) {
    word(bytes, static_cast<std::uint16_t>(value));
}

void rect(Bytes& bytes, const Rect& value) {
    for (const auto coordinate : {value.top, value.left, value.bottom, value.right}) {
        word(bytes, static_cast<std::uint16_t>(coordinate));
    }
}

Bytes fixture() {
    Bytes bytes;
    word(bytes, 0);
    rect(bytes, {10, 20, 12, 22});
    opcode(bytes, Opcode::Version);
    word(bytes, picture::version_two);
    opcode(bytes, Opcode::Header);
    bytes.resize(bytes.size() + picture::extended_header_size);
    opcode(bytes, Opcode::LongComment);
    word(bytes, 100);
    word(bytes, 1);
    bytes.push_back(42);
    bytes.push_back(0);
    opcode(bytes, Opcode::Clip);
    word(bytes, sizeof(Region));
    rect(bytes, {10, 20, 12, 21});
    opcode(bytes, Opcode::DirectBitsRect);
    bytes.insert(bytes.end(), {0, 0, 0, 255});
    word(bytes, pixmap_flag | 8);
    rect(bytes, {10, 20, 12, 22});
    word(bytes, 0);
    word(bytes, static_cast<std::uint16_t>(picture::Packing::ComponentPackBits));
    bytes.resize(bytes.size() + 12);
    word(bytes, direct_pixel_type);
    word(bytes, 32);
    word(bytes, 4);
    word(bytes, 8);
    bytes.resize(bytes.size() + 12);
    rect(bytes, {10, 20, 12, 22});
    rect(bytes, {10, 20, 12, 22});
    word(bytes, static_cast<std::uint16_t>(TransferMode::Copy));
    bytes.insert(bytes.end(), {9, 7, 255, 255, 255, 0, 0, 255, 0, 0});
    bytes.insert(bytes.end(), {9, 7, 255, 255, 0, 255, 0, 255, 255, 255});
    opcode(bytes, Opcode::End);
    return bytes;
}

void dword(Bytes& bytes, std::uint32_t value) {
    word(bytes, static_cast<std::uint16_t>(value >> 16));
    word(bytes, static_cast<std::uint16_t>(value));
}

Bytes rgb555_fixture(bool packed) {
    Bytes bytes;
    const Rect bounds{10, 20, 11, 24};
    word(bytes, 0);
    rect(bytes, bounds);
    opcode(bytes, Opcode::Version);
    word(bytes, picture::version_two);
    opcode(bytes, Opcode::DirectBitsRect);
    dword(bytes, 255);
    word(bytes, pixmap_flag | 10);
    rect(bytes, bounds);
    word(bytes, 0);
    word(bytes, static_cast<std::uint16_t>(packed ? picture::Packing::PixelPackBits
                                                  : picture::Packing::Unpacked));
    bytes.resize(bytes.size() + 12);
    word(bytes, direct_pixel_type);
    word(bytes, 16);
    word(bytes, 3);
    word(bytes, 5);
    bytes.resize(bytes.size() + 12);
    rect(bytes, bounds);
    rect(bytes, bounds);
    word(bytes, 0);
    if (packed) {
        bytes.insert(bytes.end(), {12, 128, 0, 0x7c, 0, 255, 3, 0xe0, 1, 0, 31, 0x7f, 255});
    } else {
        bytes.insert(bytes.end(), {0x7c, 0, 3, 0xe0, 3, 0xe0, 0, 31, 0x7f, 255});
    }
    if (bytes.size() & 1) {
        bytes.push_back(0);
    }
    opcode(bytes, Opcode::End);
    return bytes;
}

void verify_rgb555() {
    const Bytes expected{0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 255};
    for (const bool packed : {false, true}) {
        const auto bytes = rgb555_fixture(packed);
        const auto decoded = picture::read(bytes);
        require(decoded.bitmaps.size() == 1 && decoded.bitmaps[0].pixels == expected &&
                    decoded.bitmaps[0].stride == 16 && decoded.bitmaps[0].components == 3,
                "RGB555 decoding lost word runs, channel order or row padding.");
        for (std::size_t length = 0; length < bytes.size(); ++length) {
            bool rejected = false;
            try {
                picture::read(std::span(bytes).first(length));
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            require(rejected, "Truncated RGB555 picture was accepted.");
        }
    }
    auto overflow = rgb555_fixture(true);
    overflow[85] = 127;
    bool rejected = false;
    try {
        picture::read(overflow);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "An oversized RGB555 pixel run was accepted.");
}

Bytes compressed_fixture() {
    Bytes data;
    word(data, 0);
    for (const auto value : {65536, 0, 0, 0, 65536, 0, 2 * 65536, 3 * 65536, 0x40000000}) {
        dword(data, value);
    }
    dword(data, 0);
    rect(data, {});
    word(data, 0);
    rect(data, {0, 0, 1, 1});
    dword(data, 0);
    dword(data, 0);
    dword(data, 86);
    data.insert(data.end(), {'j', 'p', 'e', 'g'});
    data.resize(data.size() + 24);
    word(data, 1);
    word(data, 1);
    dword(data, 72 << 16);
    dword(data, 72 << 16);
    dword(data, 4);
    word(data, 1);
    data.resize(data.size() + 32);
    word(data, 24);
    word(data, 65535);
    data.insert(data.end(), {255, 216, 255, 217});
    Bytes bytes;
    word(bytes, 0);
    rect(bytes, {0, 0, 5, 5});
    opcode(bytes, Opcode::Version);
    word(bytes, picture::version_two);
    opcode(bytes, Opcode::CompressedQuickTime);
    dword(bytes, static_cast<std::uint32_t>(data.size()));
    bytes.insert(bytes.end(), data.begin(), data.end());
    opcode(bytes, Opcode::End);
    return bytes;
}

unsigned compressed_calls = 0;

void __cdecl inspect_compressed(const PixMap* pixels, const Rect* source, const Matrix* matrix,
                                std::int16_t mode, Region** mask, const PixMap* matte, const Rect*,
                                std::int16_t flags) {
    std::uint8_t** description = nullptr;
    std::uint8_t* data = nullptr;
    std::int32_t size = 0;
    require(invoke(Selector::GetCompressedPixMapInfo, address(pixels), address(&description),
                   address(&data), address(&size)) == 0,
            "Cannot obtain compressed picture metadata.");
    std::uint16_t depth = 0;
    std::memcpy(&depth, *description + 82, sizeof(depth));
    require(depth == 24 && size == 4 && data[0] == 255 && source->right == 1 &&
                matrix->values[6] == 2 * 65536 && matrix->values[7] == 3 * 65536 && !mode &&
                !mask && !matte && !flags,
            "Compressed callback lost image metadata or placement.");
    ++compressed_calls;
}

void verify_compressed() {
    const auto bytes = compressed_fixture();
    const auto picture = picture::read(bytes);
    require(picture.bitmaps.size() == 1 && picture.bitmaps[0].destination.left == 2 &&
                picture.bitmaps[0].destination.top == 3,
            "Compressed picture placement was lost.");
    auto with_fallback = bytes;
    with_fallback.resize(with_fallback.size() - 2);
    const auto indexed = indexed_fixture();
    with_fallback.insert(with_fallback.end(), indexed.begin() + 26, indexed.end());
    require(picture::read(with_fallback).bitmaps.size() == 1,
            "QuickTime's fallback bitmap covered the compressed image.");
    with_fallback.resize(bytes.size() - 2);
    with_fallback.insert(with_fallback.end(), indexed.begin() + 14, indexed.end());
    require(picture::read(with_fallback).bitmaps.size() == 2,
            "A new clipped bitmap after a compressed picture was suppressed.");
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        bool rejected = false;
        try {
            picture::read(std::span(bytes).first(length));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Truncated compressed picture was accepted.");
    }
    const Rect bounds{0, 0, 5, 5};
    Port* port = nullptr;
    require(invoke(Selector::QTNewGWorld, address(&port), bgra_format, address(&bounds)) == 0,
            "Cannot allocate compressed picture destination.");
    invoke(Selector::SetGWorld, address(port));
    Procedures procedures{};
    invoke(Selector::SetStdCProcs, address(&procedures));
    procedures.pixels = inspect_compressed;
    port->procedures = &procedures;
    const auto handle = reinterpret_cast<std::uint8_t**>(invoke(Selector::NewHandle, bytes.size()));
    std::memcpy(*handle, bytes.data(), bytes.size());
    invoke(Selector::DrawPicture, address(handle), address(&bounds));
    require(compressed_calls == 1, "Compressed picture did not call StdPix.");
    invoke(Selector::KillPicture, address(handle));
    invoke(Selector::DisposeGWorld, address(port));
}

void verify_decode(const Bytes& bytes) {
    const auto decoded = picture::read(bytes);
    require(decoded.bitmaps.size() == 1, "Picture has the wrong bitmap count.");
    const Bytes expected{0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255};
    require(decoded.bitmaps[0].pixels == expected, "Planar pixels were not converted to BGRA.");
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        bool rejected = false;
        try {
            picture::read(std::span(bytes).first(length));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "A truncated picture was accepted.");
    }
    auto invalid = bytes;
    invalid[invalid.size() - 21] = 127;
    bool rejected = false;
    try {
        picture::read(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "An oversized PackBits run was accepted.");
}

BitsProcedure original_bits = nullptr;
unsigned calls = 0;

void __cdecl inspect(const PixMap* pixels, const Rect* source, const Rect* destination,
                     std::int16_t mode, Region** mask) {
    ++calls;
    require(pixels->pixel_size == 32 && pixels->bounds.left == 20 && source->top == 10 &&
                destination->right == 4 && !mask,
            "Picture callback received incorrect metadata or transformed coordinates.");
    original_bits(pixels, source, destination, mode, mask);
}

void verify_drawing(const Bytes& bytes) {
    calls = 0;
    const auto handle = reinterpret_cast<std::uint8_t**>(invoke(Selector::NewHandle, bytes.size()));
    require(handle && *handle, "Cannot allocate picture handle.");
    std::memcpy(*handle, bytes.data(), bytes.size());
    const Rect bounds{0, 0, 4, 4};
    Port* port = nullptr;
    require(invoke(Selector::QTNewGWorld, address(&port), bgra_format, address(&bounds)) == 0,
            "Cannot allocate picture destination.");
    invoke(Selector::SetGWorld, address(port));
    Procedures procedures{};
    invoke(Selector::SetStdCProcs, address(&procedures));
    original_bits = procedures.bits;
    procedures.bits = inspect;
    port->procedures = &procedures;
    invoke(Selector::DrawPicture, address(handle), address(&bounds));
    const auto pixels = (*port->pixels)->base;
    require(calls == 1 && pixels[2] == 255 && pixels[2 * 16] == 255 && pixels[2 * 4 + 2] == 0,
            "Picture rendering lost scaling, row order or clipping.");
    invoke(Selector::ForeColor, 341);
    invoke(Selector::PaintRect, address(&bounds));
    require(pixels[3 * 4 + 1] == 255, "DrawPicture left its clipping rectangle active.");
    invoke(Selector::KillPicture, address(handle));
    invoke(Selector::DisposeGWorld, address(port));
}

}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        return 1;
    }
    const auto library = LoadLibraryW(argv[1]);
    if (!library) {
        return 1;
    }
    dispatcher = GetProcAddress(library, "theQuickTimeDispatcher");
    if (!dispatcher) {
        return 1;
    }
    int result = 0;
    try {
        const auto bytes = fixture();
        verify_decode(bytes);
        verify_rgb555();
        verify_indexed();
        invoke(Selector::QTMLInitInternals, 2);
        verify_drawing(bytes);
        verify_drawing(indexed_fixture());
        verify_compressed();
        invoke(Selector::QTMLTermInternals);
        if (argc == 3) {
            std::ifstream input(argv[2], std::ios::binary);
            require(bool(input), "Cannot read sample picture.");
            Bytes sample(std::istreambuf_iterator<char>(input), {});
            const auto decoded = picture::read(sample);
            std::cout << "Sample picture: " << decoded.bitmaps.size() << " bitmap(s).\n";
        }
        std::cout << "PICT decoding, bounds, callbacks, scaling and clipping passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        invoke(Selector::QTMLTermInternals);
        result = 1;
    }
    FreeLibrary(library);
    return result;
}
