#include "asset_io.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <stdexcept>

namespace devtools {
namespace {
using Microsoft::WRL::ComPtr;

void check(HRESULT result) {
    if (FAILED(result)) {
        throw std::runtime_error("Cannot read or encode PNG image");
    }
}

struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Apartment() {
        if (result != RPC_E_CHANGED_MODE) {
            check(result);
        }
    }

    ~Apartment() {
        if (SUCCEEDED(result)) {
            CoUninitialize();
        }
    }
};

ComPtr<IWICImagingFactory> factory() {
    ComPtr<IWICImagingFactory> result;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&result)));
    return result;
}

std::size_t image_size(unsigned width, unsigned height) {
    const auto size = std::uint64_t(width) * height * 4;
    if (!width || !height || width > 4095 || height > 4096 || size > 64 * 1024 * 1024) {
        throw std::runtime_error("Image dimensions exceed the preview limit");
    }
    return static_cast<std::size_t>(size);
}
}

std::vector<std::uint8_t> read_asset_bytes(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input ? input.tellg() : std::streampos(-1);
    if (size < std::streampos(0) || static_cast<std::uint64_t>(size) > limit) {
        throw std::runtime_error("Cannot read asset or file exceeds the size limit");
    }
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    input.seekg(0);
    if ((!result.empty() && !input.read(reinterpret_cast<char*>(result.data()),
                                        static_cast<std::streamsize>(result.size()))) ||
        input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Asset changed while being read");
    }
    return result;
}

void write_new_asset(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    if (bytes.size() > MAXDWORD) {
        throw std::runtime_error("Asset is too large to save");
    }
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Choose a new filename. Existing files are kept unchanged");
    }
    DWORD written = 0;
    const bool saved =
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != FALSE;
    if (!saved || !closed) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("Cannot save the asset file");
    }
}

media::Frame read_asset_png(const std::filesystem::path& path) {
    const auto bytes = read_asset_bytes(path);
    if (bytes.empty()) {
        throw std::runtime_error("PNG file is empty");
    }
    Apartment apartment;
    const auto imaging = factory();
    ComPtr<IWICStream> stream;
    check(imaging->CreateStream(&stream));
    check(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),
                                       static_cast<DWORD>(bytes.size())));
    ComPtr<IWICBitmapDecoder> decoder;
    check(imaging->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                           &decoder));
    GUID container{};
    check(decoder->GetContainerFormat(&container));
    if (container != GUID_ContainerFormatPng) {
        throw std::runtime_error("Import image must be a PNG file");
    }
    ComPtr<IWICBitmapFrameDecode> source;
    check(decoder->GetFrame(0, &source));
    media::Frame result;
    check(source->GetSize(&result.width, &result.height));
    result.pixels.resize(image_size(result.width, result.height));
    ComPtr<IWICFormatConverter> converted;
    check(imaging->CreateFormatConverter(&converted));
    check(converted->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom));
    check(converted->CopyPixels(nullptr, result.width * 4, static_cast<UINT>(result.pixels.size()),
                                result.pixels.data()));
    return result;
}

std::vector<std::uint8_t> asset_png_bytes(const media::Frame& frame) {
    if (frame.pixels.size() != image_size(frame.width, frame.height)) {
        throw std::runtime_error("Image pixel data does not match its dimensions");
    }
    Apartment apartment;
    const auto imaging = factory();
    ComPtr<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
    ComPtr<IWICBitmapEncoder> encoder;
    check(imaging->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> target;
    check(encoder->CreateNewFrame(&target, nullptr));
    check(target->Initialize(nullptr));
    check(target->SetSize(frame.width, frame.height));
    auto format = GUID_WICPixelFormat32bppBGRA;
    check(target->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA) {
        throw std::runtime_error("PNG encoder does not support BGRA pixels");
    }
    check(target->WritePixels(frame.height, frame.width * 4, static_cast<UINT>(frame.pixels.size()),
                              const_cast<BYTE*>(frame.pixels.data())));
    check(target->Commit());
    check(encoder->Commit());
    STATSTG info{};
    check(stream->Stat(&info, STATFLAG_NONAME));
    if (info.cbSize.QuadPart > 128 * 1024 * 1024) {
        throw std::runtime_error("Encoded PNG exceeds the size limit");
    }
    std::vector<std::uint8_t> result(static_cast<std::size_t>(info.cbSize.QuadPart));
    LARGE_INTEGER beginning{};
    check(stream->Seek(beginning, STREAM_SEEK_SET, nullptr));
    ULONG read = 0;
    check(stream->Read(result.data(), static_cast<ULONG>(result.size()), &read));
    if (read != result.size()) {
        throw std::runtime_error("Encoded PNG is incomplete");
    }
    return result;
}
}
