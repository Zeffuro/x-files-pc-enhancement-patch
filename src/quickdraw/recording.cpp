#include "image_codec.h"
#include "memory.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
using namespace quickdraw;
using Bytes = std::vector<std::uint8_t>;

#pragma pack(push, 2)

struct PictureParameters {
    Rect bounds;
    std::int32_t horizontal_resolution, vertical_resolution;
    std::int16_t version, reserved;
    std::int32_t reserved2;
};

#pragma pack(pop)

thread_local std::uint8_t** recording = nullptr;
thread_local Port* recording_port = nullptr;
thread_local Bytes recorded;

void number(Bytes& bytes, std::uint32_t value, unsigned length = 2) {
    for (unsigned i = length; i; --i) {
        bytes.push_back(static_cast<std::uint8_t>(value >> ((i - 1) * 8)));
    }
}

void rectangle(Bytes& bytes, const Rect& rect) {
    for (const auto value : {rect.top, rect.left, rect.bottom, rect.right}) {
        number(bytes, static_cast<std::uint16_t>(value));
    }
}

std::uint8_t** __cdecl open_picture(const PictureParameters* parameters) {
    if (!parameters || recording || parameters->version != -2 ||
        parameters->bounds.right <= parameters->bounds.left ||
        parameters->bounds.bottom <= parameters->bounds.top) {
        return nullptr;
    }
    recorded.clear();
    number(recorded, 0);
    rectangle(recorded, parameters->bounds);
    number(recorded, 0x11);
    number(recorded, 0x2ff);
    number(recorded, 0xc00);
    number(recorded, 0xfffe0000, 4);
    number(recorded, parameters->horizontal_resolution, 4);
    number(recorded, parameters->vertical_resolution, 4);
    rectangle(recorded, parameters->bounds);
    number(recorded, 0, 4);
    const auto allocate =
        reinterpret_cast<std::uint8_t**(__cdecl*)(std::int32_t)>(memory_entry(Selector::NewHandle));
    recording = allocate(static_cast<std::int32_t>(recorded.size()));
    if (recording) {
        std::memcpy(*recording, recorded.data(), recorded.size());
        recording_port = &drawing_port();
    }
    return recording;
}

void __cdecl close_picture() {
    if (!recording) {
        return;
    }
    if (recorded.size() % 2) {
        recorded.push_back(0);
    }
    number(recorded, 0xff);
    if (recorded.size() <= 65535) {
        recorded[0] = static_cast<std::uint8_t>(recorded.size() >> 8);
        recorded[1] = static_cast<std::uint8_t>(recorded.size());
    }
    const auto resize = reinterpret_cast<void(__cdecl*)(std::uint8_t**, std::int32_t)>(
        memory_entry(Selector::SetHandleSize));
    resize(recording, static_cast<std::int32_t>(recorded.size()));
    if (handle_bytes(recording).size() != recorded.size()) {
        unsupported(Selector::ClosePicture, "Cannot allocate recorded picture", 0);
    }
    std::memcpy(*recording, recorded.data(), recorded.size());
    release_recording();
}
}

namespace quickdraw {
bool record_image(const ImageDescription& description, std::span<const std::uint8_t> data,
                  const Rect& source, const Rect& destination, short mode) {
    if (!recording || recording_port != &drawing_port()) {
        return false;
    }
    if (source.right - source.left != destination.right - destination.left ||
        source.bottom - source.top != destination.bottom - destination.top) {
        throw std::runtime_error("Scaled compressed picture recording is unsupported");
    }
    Bytes payload;
    number(payload, 0);
    for (int i = 0; i < 9; ++i) {
        const auto value = i == 0 || i == 4 ? 65536
                           : i == 8         ? 0x40000000
                           : i == 6         ? (destination.left - source.left) * 65536
                           : i == 7         ? (destination.top - source.top) * 65536
                                            : 0;
        number(payload, value, 4);
    }
    number(payload, 0, 4);
    rectangle(payload, {});
    number(payload, mode);
    rectangle(payload, source);
    number(payload, 0, 4);
    number(payload, 0, 4);
    const auto* fields = reinterpret_cast<const std::uint8_t*>(&description);
    Bytes encoded_description(fields, fields + sizeof(description));
    // PICT stores the native ImageDescription's numeric fields in big-endian order.
    for (const auto offset : {0, 4, 8, 20, 24, 28, 36, 40, 44}) {
        std::reverse(encoded_description.begin() + offset,
                     encoded_description.begin() + offset + 4);
    }
    for (const auto offset : {12, 14, 16, 18, 32, 34, 48, 82, 84}) {
        std::swap(encoded_description[offset], encoded_description[offset + 1]);
    }
    payload.insert(payload.end(), encoded_description.begin(), encoded_description.end());
    payload.insert(payload.end(), data.begin(), data.end());
    if (recorded.size() % 2) {
        recorded.push_back(0);
    }
    if (recording_port->clip_region && *recording_port->clip_region) {
        number(recorded, 1);
        number(recorded, 10);
        rectangle(recorded, (*recording_port->clip_region)->bounds);
    }
    number(recorded, 0x8200);
    number(recorded, static_cast<std::uint32_t>(payload.size()), 4);
    recorded.insert(recorded.end(), payload.begin(), payload.end());
    return true;
}

void release_recording() {
    recording = nullptr;
    recording_port = nullptr;
    recorded.clear();
}
}

Entry recording_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::OpenCPicture, open_picture),
        bind_entry(Selector::ClosePicture, close_picture),
    };
    return find_entry(selector, entries);
}
