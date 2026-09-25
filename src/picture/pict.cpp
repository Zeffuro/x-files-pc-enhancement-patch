#include "pict.h"
#include "opcodes.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace picture {
namespace {

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::span<const std::uint8_t> take(std::size_t size) {
        if (size > bytes_.size() - position_) {
            throw std::runtime_error("PICT: truncated data");
        }
        const auto result = bytes_.subspan(position_, size);
        position_ += size;
        return result;
    }

    std::uint16_t word() {
        const auto data = take(2);
        return static_cast<std::uint16_t>((data[0] << 8) | data[1]);
    }

    std::uint32_t dword() {
        const auto high = word();
        return (std::uint32_t(high) << 16) | word();
    }

    quickdraw::Rect rectangle() {
        const auto top = static_cast<std::int16_t>(word());
        const auto left = static_cast<std::int16_t>(word());
        const auto bottom = static_cast<std::int16_t>(word());
        const auto right = static_cast<std::int16_t>(word());
        return {top, left, bottom, right};
    }

    void align() {
        take(position_ & 1);
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

std::vector<std::uint8_t> unpack(std::span<const std::uint8_t> data, std::size_t expected,
                                 std::size_t unit = 1) {
    Reader input(data);
    std::vector<std::uint8_t> row;
    row.reserve(expected);
    std::size_t consumed = 0;
    while (consumed < data.size()) {
        const auto control = input.take(1)[0];
        ++consumed;
        if (control == 128) {
            continue;
        }
        const std::size_t count = (control < 128 ? control + 1 : 257 - control) * unit;
        if (count > expected - row.size()) {
            throw std::runtime_error("PICT: packed row overflows its buffer");
        }
        if (control < 128) {
            const auto literal = input.take(count);
            row.insert(row.end(), literal.begin(), literal.end());
            consumed += count;
        } else {
            const auto value = input.take(unit);
            for (std::size_t offset = 0; offset < count; offset += unit) {
                row.insert(row.end(), value.begin(), value.end());
            }
            consumed += unit;
        }
    }
    if (row.size() != expected) {
        throw std::runtime_error("PICT: packed row has the wrong length");
    }
    return row;
}

Bitmap indexed_bitmap(Reader& input, const quickdraw::Rect& clip) {
    const auto row_bytes = input.word();
    const auto source_stride = row_bytes & quickdraw::row_bytes_mask;
    Bitmap bitmap{};
    bitmap.bounds = input.rectangle();
    unsigned depth = 1;
    std::array<std::array<std::uint8_t, 4>, 256> palette{};
    std::array<bool, 256> defined{};
    palette[0] = {255, 255, 255, 255};
    palette[1] = {0, 0, 0, 255};
    defined[0] = defined[1] = true;
    if (row_bytes & quickdraw::pixmap_flag) {
        input.word();
        const auto packing = input.word();
        input.take(12);
        const auto type = input.word();
        depth = input.word();
        const auto components = input.word();
        const auto component_size = input.word();
        input.take(12);
        if (packing != 0 || type != 0 || components != 1 || component_size != depth ||
            (depth != 1 && depth != 2 && depth != 4 && depth != 8)) {
            throw std::runtime_error("PICT: unsupported indexed-pixel format");
        }
        input.dword();
        const auto flags = input.word();
        const auto last = input.word();
        if (last >= palette.size()) {
            throw std::runtime_error("PICT: invalid color table size");
        }
        defined.fill(false);
        for (unsigned entry = 0; entry <= last; ++entry) {
            const auto value = input.word();
            const auto index = flags & 0x8000 ? entry : value;
            if (index >= (1u << depth)) {
                throw std::runtime_error("PICT: invalid color table index");
            }
            const auto red = static_cast<std::uint8_t>(input.word() >> 8);
            const auto green = static_cast<std::uint8_t>(input.word() >> 8);
            const auto blue = static_cast<std::uint8_t>(input.word() >> 8);
            palette[index] = {blue, green, red, 255};
            defined[index] = true;
        }
    }
    bitmap.source = input.rectangle();
    bitmap.destination = input.rectangle();
    bitmap.mode = static_cast<std::int16_t>(input.word());
    bitmap.clip = clip;
    const int width = bitmap.bounds.right - bitmap.bounds.left;
    const int height = bitmap.bounds.bottom - bitmap.bounds.top;
    if (width <= 0 || height <= 0 || width > quickdraw::row_bytes_mask / 4 ||
        static_cast<unsigned>(source_stride) < (width * depth + 7) / 8) {
        throw std::runtime_error("PICT: invalid indexed bitmap dimensions");
    }
    bitmap.stride = static_cast<std::uint16_t>(width * 4);
    bitmap.components = 3;
    if (std::uint64_t(bitmap.stride) * height > 64 * 1024 * 1024) {
        throw std::runtime_error("PICT: bitmap is too large");
    }
    bitmap.pixels.resize(std::size_t(bitmap.stride) * height);
    for (int y = 0; y < height; ++y) {
        std::vector<std::uint8_t> row;
        if (source_stride >= 8) {
            const auto length = source_stride > 250 ? input.word() : input.take(1)[0];
            row = unpack(input.take(length), source_stride);
        } else {
            const auto raw = input.take(source_stride);
            row.assign(raw.begin(), raw.end());
        }
        auto* destination = bitmap.pixels.data() + std::size_t(y) * bitmap.stride;
        for (int x = 0; x < width; ++x) {
            const auto bit = x * depth;
            const auto index = (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
            if (!defined[index]) {
                throw std::runtime_error("PICT: missing palette color");
            }
            std::copy(palette[index].begin(), palette[index].end(), destination + 4 * x);
        }
    }
    return bitmap;
}

Bitmap direct_bitmap(Reader& input, const quickdraw::Rect& clip) {
    input.take(4);
    const auto row_bytes = input.word();
    Bitmap bitmap{};
    bitmap.bounds = input.rectangle();
    input.word();
    const auto packing = static_cast<Packing>(input.word());
    input.take(12);
    const auto type = input.word();
    const auto depth = input.word();
    const auto components = input.word();
    bitmap.components = components;
    const auto component_size = input.word();
    input.take(12);
    bitmap.source = input.rectangle();
    bitmap.destination = input.rectangle();
    bitmap.mode = static_cast<std::int16_t>(input.word());
    bitmap.clip = clip;
    const auto source_stride = row_bytes & quickdraw::row_bytes_mask;

    const int width = bitmap.bounds.right - bitmap.bounds.left;
    const int height = bitmap.bounds.bottom - bitmap.bounds.top;
    const bool rgb555 = depth == 16 && component_size == 5 && components == 3 &&
                        (packing == Packing::Unpacked || packing == Packing::PixelPackBits);
    const bool rgb888 = depth == 32 && component_size == 8 &&
                        (components == 3 || components == 4) &&
                        (packing == Packing::Unpacked || packing == Packing::DropPadding ||
                         packing == Packing::ComponentPackBits);
    if (!(row_bytes & quickdraw::pixmap_flag) || type != quickdraw::direct_pixel_type ||
        (!rgb555 && !rgb888)) {
        throw std::runtime_error("PICT: unsupported direct-pixel format");
    }
    const auto pixel_size = depth / 8;
    if (width <= 0 || height <= 0 || width > quickdraw::row_bytes_mask / 4 ||
        source_stride < width * pixel_size || source_stride % pixel_size) {
        throw std::runtime_error("PICT: invalid bitmap dimensions");
    }
    bitmap.stride = static_cast<std::uint16_t>(rgb555 ? width * 4 : source_stride);
    if (std::uint64_t(bitmap.stride) * height > 64 * 1024 * 1024) {
        throw std::runtime_error("PICT: bitmap is too large");
    }
    bitmap.pixels.resize(std::size_t(bitmap.stride) * height);
    const std::size_t plane_size = bitmap.stride / 4;
    for (int y = 0; y < height; ++y) {
        std::vector<std::uint8_t> row;
        const bool planar = packing == Packing::ComponentPackBits && source_stride >= 8;
        const bool packed_pixels = packing == Packing::PixelPackBits && source_stride >= 8;
        if (planar || packed_pixels) {
            const auto length = source_stride > 250 ? input.word() : input.take(1)[0];
            row = unpack(input.take(length), planar ? plane_size * components : source_stride,
                         packed_pixels ? 2 : 1);
        } else {
            const auto length = packing == Packing::DropPadding ? plane_size * 3 : source_stride;
            const auto raw = input.take(length);
            row.assign(raw.begin(), raw.end());
        }
        auto* destination = bitmap.pixels.data() + std::size_t(y) * bitmap.stride;
        for (int x = 0; x < width; ++x) {
            if (rgb555) {
                const auto pixel = (row[2 * x] << 8) | row[2 * x + 1];
                for (int channel = 0; channel < 3; ++channel) {
                    const auto value = (pixel >> (channel * 5)) & 31;
                    destination[4 * x + channel] =
                        static_cast<std::uint8_t>((value << 3) | (value >> 2));
                }
                destination[4 * x + 3] = 255;
            } else if (planar) {
                const auto red = components == 4 ? plane_size : 0;
                destination[4 * x] = row[red + 2 * plane_size + x];
                destination[4 * x + 1] = row[red + plane_size + x];
                destination[4 * x + 2] = row[red + x];
                destination[4 * x + 3] = components == 4 ? row[x] : 255;
            } else {
                const auto red =
                    packing == Packing::DropPadding ? std::size_t(x) * 3 : std::size_t(x) * 4 + 1;
                destination[4 * x] = row[red + 2];
                destination[4 * x + 1] = row[red + 1];
                destination[4 * x + 2] = row[red];
                destination[4 * x + 3] = packing == Packing::DropPadding ? 255 : row[red - 1];
            }
        }
    }
    return bitmap;
}

Bitmap compressed_bitmap(Reader& stream, const quickdraw::Rect& clip) {
    Reader input(stream.take(stream.dword()));
    if (input.word() != 0) {
        throw std::runtime_error("PICT: unknown compressed image version");
    }
    std::int32_t matrix[9];
    for (auto& value : matrix) {
        value = static_cast<std::int32_t>(input.dword());
    }
    const auto matte = input.dword();
    input.rectangle();
    Bitmap bitmap{};
    bitmap.mode = static_cast<std::int16_t>(input.word());
    bitmap.source = input.rectangle();
    bitmap.clip = clip;
    input.dword();
    const auto mask = input.dword();
    if (matte || mask || matrix[0] != 65536 || matrix[4] != 65536 || matrix[8] != 0x40000000 ||
        matrix[1] || matrix[2] || matrix[3] || matrix[5] || matrix[6] % 65536 ||
        matrix[7] % 65536) {
        throw std::runtime_error("PICT: unsupported compressed image transform or mask");
    }
    const auto size = input.dword();
    if (size != 86) {
        throw std::runtime_error("PICT: unsupported image description");
    }
    const auto description = input.take(size - 4);
    bitmap.description.resize(size);
    auto put = [&](std::size_t offset, std::uint32_t value, unsigned length) {
        for (unsigned byte = 0; byte < length; ++byte) {
            bitmap.description[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
        }
    };
    put(0, size, 4);
    std::copy(description.begin(), description.end(), bitmap.description.begin() + 4);
    auto field = [&](std::size_t offset, unsigned length) {
        std::uint32_t value = 0;
        for (unsigned byte = 0; byte < length; ++byte) {
            value = (value << 8) | description[offset - 4 + byte];
        }
        put(offset, value, length);
        return value;
    };
    for (const auto offset : {4, 8, 20, 24, 28, 36, 40}) {
        field(offset, 4);
    }
    for (const auto offset : {12, 14, 16, 18, 48, 84}) {
        field(offset, 2);
    }
    const auto width = field(32, 2);
    const auto height = field(34, 2);
    const auto payload = field(44, 4);
    const auto depth = field(82, 2);
    if (!width || !height || width > 4095 || height > 4096 || depth != 24) {
        throw std::runtime_error("PICT: invalid compressed image dimensions or depth");
    }
    bitmap.bounds = {0, 0, static_cast<std::int16_t>(height), static_cast<std::int16_t>(width)};
    bitmap.destination = bitmap.source;
    auto translate = [](std::int16_t value, std::int32_t offset) {
        const auto result = value + offset / 65536;
        if (result < -32768 || result > 32767) {
            throw std::runtime_error("PICT: image offset overflow");
        }
        return static_cast<std::int16_t>(result);
    };
    bitmap.destination.left = translate(bitmap.source.left, matrix[6]);
    bitmap.destination.right = translate(bitmap.source.right, matrix[6]);
    bitmap.destination.top = translate(bitmap.source.top, matrix[7]);
    bitmap.destination.bottom = translate(bitmap.source.bottom, matrix[7]);
    bitmap.stride = static_cast<std::uint16_t>(width * 4);
    bitmap.components = 3;
    const auto packet = input.take(payload);
    bitmap.compressed.assign(packet.begin(), packet.end());
    return bitmap;
}

}

Picture read(std::span<const std::uint8_t> bytes) {
    Reader input(bytes);
    input.word();
    Picture picture;
    picture.frame = input.rectangle();
    if (input.word() != static_cast<std::uint16_t>(Opcode::Version) ||
        input.word() != version_two) {
        throw std::runtime_error("PICT: only version 2 pictures are supported");
    }
    auto clip = picture.frame;
    bool compressed = false;
    bool fallback = false;
    for (;;) {
        input.align();
        const auto opcode = static_cast<Opcode>(input.word());
        switch (opcode) {
            case Opcode::Noop:
            case Opcode::DefaultHighlight:
                break;
            case Opcode::Clip:
                if (input.word() != sizeof(quickdraw::Region)) {
                    throw std::runtime_error("PICT: complex clip regions are not supported");
                }
                clip = input.rectangle();
                fallback = false;
                break;
            case Opcode::CompressedQuickTime:
                picture.bitmaps.push_back(compressed_bitmap(input, clip));
                compressed = true;
                fallback = true;
                break;
            case Opcode::TextFont:
            case Opcode::TextSize:
            case Opcode::PenSize:
            case Opcode::TextFace:
            case Opcode::TextRatio:
            case Opcode::LongText:
                if (!compressed) {
                    throw std::runtime_error("PICT: standalone text is unsupported");
                }
                // QuickTime pictures include a fallback warning for readers without a decoder.
                if (opcode == Opcode::LongText) {
                    input.take(4);
                    input.take(input.take(1)[0]);
                } else {
                    const auto length = opcode == Opcode::TextFace    ? 1
                                        : opcode == Opcode::PenSize   ? 4
                                        : opcode == Opcode::TextRatio ? 8
                                                                      : 2;
                    input.take(length);
                }
                break;
            case Opcode::DirectBitsRect:
                picture.bitmaps.push_back(direct_bitmap(input, clip));
                break;
            case Opcode::PackBitsRect: {
                auto bitmap = indexed_bitmap(input, clip);
                // Legacy QuickTime pictures append a bitmap warning for non-QuickTime readers.
                if (!fallback) {
                    picture.bitmaps.push_back(std::move(bitmap));
                }
                break;
            }
            case Opcode::ShortComment:
                input.word();
                break;
            case Opcode::LongComment:
                input.word();
                input.take(input.word());
                break;
            case Opcode::Header:
                input.take(extended_header_size);
                break;
            case Opcode::End:
                return picture;
            default:
                throw std::runtime_error("PICT: unsupported opcode " +
                                         std::to_string(static_cast<std::uint16_t>(opcode)));
        }
    }
}

}
