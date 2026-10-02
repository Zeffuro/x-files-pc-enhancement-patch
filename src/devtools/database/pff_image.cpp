#include "pff_image.h"
#include "picture/pict.h"
#include "picture/opcodes.h"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace devtools {
namespace {
constexpr std::size_t byte_limit = 64 * 1024 * 1024;

std::size_t image_size(unsigned width, unsigned height) {
    if (!width || !height || width > 32767 || height > 32767 ||
        std::uint64_t(width) * height > byte_limit / 4) {
        throw std::runtime_error("PICT image dimensions exceed the 64 MiB safety limit");
    }
    return std::size_t(width) * height * 4;
}

int width(const quickdraw::Rect& rectangle) {
    return int(rectangle.right) - rectangle.left;
}

int height(const quickdraw::Rect& rectangle) {
    return int(rectangle.bottom) - rectangle.top;
}

void composite(media::Frame& frame, const quickdraw::Rect& canvas, const picture::Bitmap& bitmap,
               std::span<const std::uint8_t> pixels, std::size_t stride) {
    if (bitmap.mode != static_cast<std::int16_t>(quickdraw::TransferMode::Copy) &&
        bitmap.mode != static_cast<std::int16_t>(quickdraw::TransferMode::DitherCopy)) {
        throw std::runtime_error("PICT transfer mode is unsupported: " +
                                 std::to_string(bitmap.mode));
    }
    const auto bw = width(bitmap.bounds), bh = height(bitmap.bounds);
    const auto sw = width(bitmap.source), sh = height(bitmap.source);
    const auto dw = width(bitmap.destination), dh = height(bitmap.destination);
    if (bw <= 0 || bh <= 0 || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 ||
        stride < std::size_t(bw) * 4 || std::uint64_t(stride) * bh > pixels.size()) {
        throw std::runtime_error("PICT bitmap geometry or pixel storage is invalid");
    }
    const auto left =
        std::max({int(canvas.left), int(bitmap.destination.left), int(bitmap.clip.left)});
    const auto top = std::max({int(canvas.top), int(bitmap.destination.top), int(bitmap.clip.top)});
    const auto right =
        std::min({int(canvas.right), int(bitmap.destination.right), int(bitmap.clip.right)});
    const auto bottom =
        std::min({int(canvas.bottom), int(bitmap.destination.bottom), int(bitmap.clip.bottom)});
    for (int y = top; y < bottom; ++y) {
        const auto sy = bitmap.source.top + std::int64_t(y - bitmap.destination.top) * sh / dh -
                        bitmap.bounds.top;
        if (sy < 0 || sy >= bh) {
            continue;
        }
        for (int x = left; x < right; ++x) {
            const auto sx = bitmap.source.left +
                            std::int64_t(x - bitmap.destination.left) * sw / dw -
                            bitmap.bounds.left;
            if (sx < 0 || sx >= bw) {
                continue;
            }
            const auto source = std::size_t(sy) * stride + std::size_t(sx) * 4;
            const auto destination =
                (std::size_t(y - canvas.top) * frame.width + x - canvas.left) * 4;
            std::copy_n(pixels.data() + source, 3, frame.pixels.data() + destination);
            frame.pixels[destination + 3] = 255;
        }
    }
}

void word(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void dword(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    word(bytes, static_cast<std::uint16_t>(value >> 16));
    word(bytes, static_cast<std::uint16_t>(value));
}

void rectangle(std::vector<std::uint8_t>& bytes, const quickdraw::Rect& value) {
    for (const auto coordinate : {value.top, value.left, value.bottom, value.right}) {
        word(bytes, static_cast<std::uint16_t>(coordinate));
    }
}
}

media::Frame decode_pff_image(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > byte_limit) {
        throw std::runtime_error("PICT input exceeds the 64 MiB safety limit");
    }
    const auto picture = picture::read(bytes, {1024, byte_limit, true});
    const auto w = width(picture.frame), h = height(picture.frame);
    if (w <= 0 || h <= 0 || picture.bitmaps.empty()) {
        throw std::runtime_error("PICT has no drawable image or its frame is invalid");
    }
    media::Frame frame;
    frame.width = static_cast<unsigned>(w);
    frame.height = static_cast<unsigned>(h);
    frame.pixels.assign(image_size(frame.width, frame.height), 255);
    media::Video decoder;
    for (const auto& bitmap : picture.bitmaps) {
        if (bitmap.compressed.empty()) {
            composite(frame, picture.frame, bitmap, bitmap.pixels, bitmap.stride);
        } else {
            if (bitmap.description.size() < 8) {
                throw std::runtime_error("PICT compressed image description is incomplete");
            }
            media::Description format;
            for (int byte = 7; byte >= 4; --byte) {
                format.codec += static_cast<char>(bitmap.description[byte]);
            }
            format.width = static_cast<std::uint16_t>(width(bitmap.bounds));
            format.height = static_cast<std::uint16_t>(height(bitmap.bounds));
            format.depth = 24;
            image_size(format.width, format.height);
            const auto& decoded = decoder.image(format, bitmap.compressed);
            if (decoded.width != format.width || decoded.height != format.height ||
                decoded.pixels.size() != image_size(decoded.width, decoded.height)) {
                throw std::runtime_error("PICT decoded dimensions do not match its bitmap");
            }
            composite(frame, picture.frame, bitmap, decoded.pixels, std::size_t(decoded.width) * 4);
        }
    }
    return frame;
}

std::vector<std::uint8_t> encode_pff_image(const media::Frame& frame) {
    const auto size = image_size(frame.width, frame.height);
    if (frame.width > quickdraw::row_bytes_mask / 4 || frame.pixels.size() != size) {
        throw std::runtime_error("PICT import dimensions or pixel storage are unsupported");
    }
    for (std::size_t at = 3; at < size; at += 4) {
        if (frame.pixels[at] != 255) {
            throw std::runtime_error("PNG transparency is unsupported. Import an opaque image.");
        }
    }
    const quickdraw::Rect bounds{0, 0, static_cast<std::int16_t>(frame.height),
                                 static_cast<std::int16_t>(frame.width)};
    std::vector<std::uint8_t> bytes;
    bytes.reserve(size + 128);
    word(bytes, 0);
    rectangle(bytes, bounds);
    word(bytes, static_cast<std::uint16_t>(picture::Opcode::Version));
    word(bytes, picture::version_two);
    word(bytes, static_cast<std::uint16_t>(picture::Opcode::Header));
    dword(bytes, 0xfffffffe);
    dword(bytes, 72 << 16);
    dword(bytes, 72 << 16);
    rectangle(bytes, bounds);
    dword(bytes, 0);
    word(bytes, static_cast<std::uint16_t>(picture::Opcode::Clip));
    word(bytes, sizeof(quickdraw::Region));
    rectangle(bytes, bounds);
    word(bytes, static_cast<std::uint16_t>(picture::Opcode::DirectBitsRect));
    dword(bytes, 255);
    word(bytes, quickdraw::pixmap_flag | static_cast<std::uint16_t>(frame.width * 4));
    rectangle(bytes, bounds);
    word(bytes, 0);
    word(bytes, static_cast<std::uint16_t>(picture::Packing::Unpacked));
    dword(bytes, 0);
    dword(bytes, 72 << 16);
    dword(bytes, 72 << 16);
    word(bytes, quickdraw::direct_pixel_type);
    word(bytes, 32);
    word(bytes, 3);
    word(bytes, 8);
    dword(bytes, 0);
    dword(bytes, 0);
    dword(bytes, 0);
    rectangle(bytes, bounds);
    rectangle(bytes, bounds);
    word(bytes, static_cast<std::uint16_t>(quickdraw::TransferMode::Copy));
    for (std::size_t at = 0; at < size; at += 4) {
        bytes.insert(bytes.end(),
                     {255, frame.pixels[at + 2], frame.pixels[at + 1], frame.pixels[at]});
    }
    word(bytes, static_cast<std::uint16_t>(picture::Opcode::End));
    if (bytes.size() <= UINT16_MAX) {
        bytes[0] = static_cast<std::uint8_t>(bytes.size() >> 8);
        bytes[1] = static_cast<std::uint8_t>(bytes.size());
    }
    return bytes;
}
}
