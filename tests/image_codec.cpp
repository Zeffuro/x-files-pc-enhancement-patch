#include "dispatch.h"
#include "quickdraw/image_codec.h"
#include "picture/pict.h"

#include <cstring>
#include <iostream>
#include <vector>

using namespace test;
using namespace quickdraw;

void round_trip(std::uint32_t pixel_format) {
    const Rect bounds{10, 20, 42, 68}, area{14, 24, 38, 64};
    Port* port = nullptr;
    require(invoke(Selector::QTNewGWorld, address(&port), pixel_format, address(&bounds)) == 0,
            "Cannot create source world");
    invoke(Selector::SetGWorld, address(port));
    auto& map = **port->pixels;
    const unsigned stride = map.row_bytes & row_bytes_mask;
    for (unsigned y = 0; y < 32; ++y) {
        for (unsigned x = 0; x < 48; ++x) {
            auto* p = map.base + y * stride + x * (map.pixel_size / 8);
            p[0] = y < 16 ? 10 : 230;
            p[1] = 50;
            p[2] = y < 16 ? 230 : 10;
        }
    }
    std::int32_t maximum = 0;
    require(invoke(Selector::GetMaxCompressionSize, address(port->pixels), address(&area), 0, 0x300,
                   0x6a706567, 0, address(&maximum)) == 0 &&
                maximum > 0,
            "Missing compression size");
    std::vector<std::uint8_t> encoded(maximum + 16, 0xcd);
    const auto handle = reinterpret_cast<std::uint8_t**>(invoke(Selector::NewHandle, 86));
    require(invoke(Selector::CompressImage, address(port->pixels), address(&area), 0x300,
                   0x6a706567, address(handle), address(encoded.data())) == 0,
            "JPEG encoding failed");
    ImageDescription description{};
    std::memcpy(&description, *handle, sizeof(description));
    require(description.width == 40 && description.height == 24 &&
                description.codec == 0x6a706567 && description.data_size > 0 &&
                description.data_size <= maximum,
            "Incorrect image description");
    for (std::size_t i = description.data_size; i < encoded.size(); ++i) {
        require(encoded[i] == 0xcd, "Compression overwrote the output buffer");
    }

    struct {
        Rect bounds;
        std::int32_t hres, vres;
        std::int16_t version, reserved;
        std::int32_t reserved2;
    } parameters{{0, 0, 24, 40}, 72 << 16, 72 << 16, -2, 0, 0};

    const auto picture =
        reinterpret_cast<std::uint8_t**>(invoke(Selector::OpenCPicture, address(&parameters)));
    require(picture != nullptr, "Cannot open picture recording");
    invoke(Selector::ClipRect, address(&parameters.bounds));
    require(invoke(Selector::DecompressImage, address(encoded.data()), address(handle),
                   address(port->pixels), address(&parameters.bounds), address(&parameters.bounds),
                   0, 0) == 0,
            "Cannot record compressed image");
    invoke(Selector::ClosePicture);
    const auto size = invoke(Selector::GetHandleSize, address(picture));
    const std::vector<std::uint8_t> saved(*picture, *picture + size);
    invoke(Selector::KillPicture, address(picture));
    const auto parsed = picture::read(saved);
    require(parsed.bitmaps.size() == 1 && parsed.bitmaps[0].compressed.size() ==
                                              static_cast<std::size_t>(description.data_size),
            "Recorded PICT is invalid");
    const auto loaded = reinterpret_cast<std::uint8_t**>(invoke(Selector::NewHandle, size));
    std::memcpy(*loaded, saved.data(), size);
    Port* output = nullptr;
    require(invoke(Selector::QTNewGWorld, address(&output), bgra_format,
                   address(&parameters.bounds)) == 0,
            "Cannot create load destination");
    invoke(Selector::SetGWorld, address(output));
    invoke(Selector::DrawPicture, address(loaded), address(&parameters.bounds));
    const auto pixels = (*output->pixels)->base;
    require(pixels[2] > 200 && pixels[0] < 40 && pixels[20 * 160] > 200 &&
                pixels[20 * 160 + 2] < 40,
            "Photo colors or row order changed on reload");
    require(invoke(Selector::DecompressImage, address(encoded.data()), address(handle),
                   address(output->pixels), address(&parameters.bounds),
                   address(&parameters.bounds), 0, 0) == 0,
            "Standalone JPEG decompression failed");
    const Rect invalid{0, 0, 500, 500};
    require(static_cast<short>(invoke(Selector::GetMaxCompressionSize, address(port->pixels),
                                      address(&invalid), 0, 0x300, 0x6a706567, 0,
                                      address(&maximum))) == -50,
            "Out-of-bounds compression was accepted");
    invoke(Selector::DisposeHandle, address(handle));
    invoke(Selector::KillPicture, address(loaded));
    invoke(Selector::DisposeGWorld, address(output));
    invoke(Selector::DisposeGWorld, address(port));
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto library = LoadLibraryW(argv[1]);
    if (!library) {
        return 1;
    }
    dispatcher = GetProcAddress(library, "theQuickTimeDispatcher");
    try {
        require(dispatcher != nullptr, "No dispatcher");
        invoke(Selector::QTMLInitInternals, 2);
        round_trip(bgr_format);
        round_trip(bgra_format);
        invoke(Selector::QTMLTermInternals);
        std::cout << "JPEG photo compression, PICT serialization and reload passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    FreeLibrary(library);
}
