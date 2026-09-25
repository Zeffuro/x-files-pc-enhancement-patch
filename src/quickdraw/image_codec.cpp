#include "image_codec.h"
#include "memory.h"
#include "media/video.h"
#include "regions.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
using namespace quickdraw;
using Microsoft::WRL::ComPtr;
constexpr std::uint32_t jpeg = 0x6a706567;

void check(HRESULT result) {
    if (FAILED(result)) {
        throw std::runtime_error("JPEG encoding failed");
    }
}

bool valid(PixMapHandle map, const Rect* area) {
    if (!map || !*map || !(*map)->base || !area) {
        return false;
    }
    const auto& p = **map;
    const auto channels = p.pixel_format == bgra_format ? 4 : p.pixel_format == bgr_format ? 3 : 0;
    return channels && p.pixel_size == channels * 8 && area->right > area->left &&
           area->bottom > area->top && area->left >= p.bounds.left && area->top >= p.bounds.top &&
           area->right <= p.bounds.right && area->bottom <= p.bounds.bottom &&
           (p.row_bytes & row_bytes_mask) >= (p.bounds.right - p.bounds.left) * channels;
}

std::vector<std::uint8_t> encode(const PixMap& map, const Rect& area, std::uint32_t quality) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    struct Apartment {
        HRESULT status;

        ~Apartment() {
            if (SUCCEEDED(status)) {
                CoUninitialize();
            }
        }
    } apartment{initialized};

    if (initialized != RPC_E_CHANGED_MODE) {
        check(initialized);
    }
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory)));
    ComPtr<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder));
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    check(encoder->CreateNewFrame(&frame, &options));
    PROPBAG2 property{};
    property.pstrName = const_cast<wchar_t*>(L"ImageQuality");
    VARIANT value{};
    value.vt = VT_R4;
    value.fltVal = std::min(quality, 1023u) / 1023.0f;
    check(options->Write(1, &property, &value));
    check(frame->Initialize(options.Get()));
    const unsigned width = area.right - area.left, height = area.bottom - area.top;
    check(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat24bppBGR;
    check(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat24bppBGR) {
        throw std::runtime_error("JPEG encoder rejected BGR pixels");
    }
    std::vector<BYTE> pixels(width * height * 3);
    const unsigned channels = map.pixel_size / 8;
    GdiFlush();
    for (unsigned y = 0; y < height; ++y) {
        const auto* row = map.base +
                          (y + area.top - map.bounds.top) * (map.row_bytes & row_bytes_mask) +
                          (area.left - map.bounds.left) * channels;
        for (unsigned x = 0; x < width; ++x) {
            std::memcpy(pixels.data() + (y * width + x) * 3, row + x * channels, 3);
        }
    }
    check(frame->WritePixels(height, width * 3, static_cast<UINT>(pixels.size()), pixels.data()));
    check(frame->Commit());
    check(encoder->Commit());
    STATSTG stat{};
    check(stream->Stat(&stat, STATFLAG_NONAME));
    if (stat.cbSize.QuadPart > width * height * 4ull + 65536) {
        throw std::runtime_error("JPEG exceeds compression buffer bound");
    }
    std::vector<std::uint8_t> bytes(stat.cbSize.LowPart);
    check(stream->Seek({}, STREAM_SEEK_SET, nullptr));
    ULONG read = 0;
    check(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read));
    if (read != bytes.size()) {
        throw std::runtime_error("Truncated encoded JPEG");
    }
    return bytes;
}

short __cdecl maximum_size(PixMapHandle map, const Rect* area, short depth, std::uint32_t,
                           std::uint32_t codec, void* component, std::int32_t* size) {
    if (!size || !valid(map, area) || codec != jpeg || (depth && depth != 24) || component) {
        return -50;
    }
    *size = (area->right - area->left) * (area->bottom - area->top) * 4 + 65536;
    return 0;
}

short __cdecl compress(PixMapHandle map, const Rect* area, std::uint32_t quality,
                       std::uint32_t codec, ImageDescription** description, std::uint8_t* data) {
    if (!valid(map, area) || codec != jpeg || !description || !data ||
        handle_bytes(reinterpret_cast<std::uint8_t**>(description)).size() <
            sizeof(ImageDescription)) {
        return -50;
    }
    try {
        const auto encoded = encode(**map, *area, quality);
        ImageDescription result{};
        result.size = sizeof(result);
        result.codec = jpeg;
        result.spatial_quality = quality;
        result.width = area->right - area->left;
        result.height = area->bottom - area->top;
        result.horizontal_resolution = result.vertical_resolution = 72 << 16;
        result.data_size = static_cast<std::int32_t>(encoded.size());
        result.frames = 1;
        result.name[0] = 4;
        std::memcpy(result.name + 1, "JPEG", 4);
        result.depth = 24;
        result.color_table = -1;
        **description = result;
        std::memcpy(data, encoded.data(), encoded.size());
        return 0;
    } catch (const std::exception&) {
        return -108;
    }
}

short __cdecl decompress(const std::uint8_t* data, ImageDescription** description,
                         PixMapHandle destination, const Rect* source, const Rect* target,
                         short mode, RegionHandle mask) {
    if (!data || !description || !*description || !destination || !*destination || !source ||
        !target || mask || (mode != 0 && mode != 64)) {
        return -50;
    }
    const auto& d = **description;
    if (d.size != sizeof(d) || d.codec != jpeg || d.width <= 0 || d.height <= 0 || d.width > 4095 ||
        d.height > 4096 || d.data_size <= 0 || d.data_size > 128 * 1024 * 1024 ||
        source->left < 0 || source->top < 0 || source->right > d.width ||
        source->bottom > d.height || source->left >= source->right ||
        source->top >= source->bottom) {
        return -50;
    }
    try {
        const std::span packet(data, static_cast<std::size_t>(d.data_size));
        if (record_image(d, packet, *source, *target, mode)) {
            return 0;
        }
        const auto dc = pixel_dc(*destination);
        if (!dc) {
            return -50;
        }
        media::Video decoder;
        media::Description format;
        format.codec = "jpeg";
        format.width = d.width;
        format.height = d.height;
        format.depth = 24;
        const auto& frame = decoder.image(format, packet);
        if (frame.width != static_cast<unsigned>(d.width) ||
            frame.height != static_cast<unsigned>(d.height)) {
            return -50;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = d.width;
        info.bmiHeader.biHeight = -d.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        const auto result = StretchDIBits(
            dc, target->left, target->top, target->right - target->left,
            target->bottom - target->top, source->left, source->top, source->right - source->left,
            source->bottom - source->top, frame.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        GdiFlush();
        return result == GDI_ERROR ? -50 : 0;
    } catch (const std::exception&) {
        return -50;
    }
}
}

Entry image_codec_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::GetMaxCompressionSize, maximum_size),
        bind_entry(Selector::CompressImage, compress),
        bind_entry(Selector::DecompressImage, decompress),
    };
    return find_entry(selector, entries);
}
