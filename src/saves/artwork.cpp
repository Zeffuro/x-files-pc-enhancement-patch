#include "artwork.h"
#include "picture/pict.h"
#include "media/video.h"
#include <charconv>
#include <fstream>
#include <stdexcept>

namespace saves {
namespace {
unsigned number(std::string_view data, std::size_t& at) {
    if (at >= data.size()) {
        throw std::runtime_error("Missing artwork field");
    }
    const auto digits = static_cast<unsigned char>(data[at++]);
    unsigned length = 0;
    if (!digits || digits > 4 || at + digits > data.size()) {
        throw std::runtime_error("Invalid artwork field");
    }
    const auto end = data.data() + at + digits;
    const auto parsed = std::from_chars(data.data() + at, end, length);
    at += digits;
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        throw std::runtime_error("Invalid artwork string");
    }
    return length;
}

std::string field(std::string_view data, std::size_t& at) {
    const auto length = number(data, at);
    if (length > 1024 || at + length > data.size()) {
        throw std::runtime_error("Invalid artwork string");
    }
    auto result = std::string(data.substr(at, length));
    at += length;
    return result;
}

std::vector<std::uint8_t> asset(const std::filesystem::path& game, std::string_view filename) {
    const auto hdb = game / L"XFiles.hdb";
    const auto length = std::filesystem::file_size(hdb);
    if (length > 128 * 1024 * 1024) {
        throw std::runtime_error("Game database is too large");
    }
    std::string data(static_cast<std::size_t>(length), '\0');
    std::ifstream input(hdb, std::ios::binary);
    input.read(data.data(), data.size());
    const auto key = std::string(1, '\x7f') + std::string(filename);
    auto at = data.find(key);
    if (!input || at == std::string::npos) {
        throw std::runtime_error("Save screen artwork is unavailable");
    }
    at += key.size();
    number(data, at);
    number(data, at);
    const auto archive = field(data, at);
    const auto index = number(data, at);
    const auto duplicate = number(data, at);
    if (archive != "x.pff" || index != duplicate) {
        throw std::runtime_error("Unrecognized save artwork reference");
    }
    std::ifstream pff(game / L"X.PFF", std::ios::binary);
    std::array<char, 4> signature{};
    std::uint32_t count = 0, begin = 0, end = 0;
    pff.read(signature.data(), signature.size());
    pff.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (signature != std::array<char, 4>{'P', 'F', 'F', ' '} || count > 10000 || index >= count) {
        throw std::runtime_error("Invalid artwork archive");
    }
    pff.seekg(8 + static_cast<std::streamoff>(index) * 4);
    pff.read(reinterpret_cast<char*>(&begin), sizeof(begin));
    pff.read(reinterpret_cast<char*>(&end), sizeof(end));
    if (!pff || begin < 8 + (count + 1) * 4 || end <= begin || end - begin > 8 * 1024 * 1024 ||
        end > std::filesystem::file_size(game / L"X.PFF")) {
        throw std::runtime_error("Invalid artwork archive entry");
    }
    std::vector<std::uint8_t> bytes(end - begin);
    pff.seekg(begin);
    pff.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!pff) {
        throw std::runtime_error("Incomplete artwork archive entry");
    }
    return bytes;
}
}

bool draw_artwork(HDC dc, const std::filesystem::path& game, std::string_view filename,
                  const RECT& destination) {
    try {
        const auto decoded = picture::read(asset(game, filename));
        const auto width = decoded.frame.right - decoded.frame.left;
        const auto height = decoded.frame.bottom - decoded.frame.top;
        if (width <= 0 || height <= 0) {
            return false;
        }
        for (const auto& bitmap : decoded.bitmaps) {
            media::Frame frame;
            const auto* pixels = bitmap.pixels.data();
            unsigned stride = bitmap.stride / 4;
            unsigned rows = bitmap.bounds.bottom - bitmap.bounds.top;
            if (!bitmap.compressed.empty()) {
                if (bitmap.description.size() < 8) {
                    return false;
                }
                media::Description format;
                for (int byte = 7; byte >= 4; --byte) {
                    format.codec += static_cast<char>(bitmap.description[byte]);
                }
                format.width = static_cast<std::uint16_t>(bitmap.bounds.right - bitmap.bounds.left);
                format.height = static_cast<std::uint16_t>(rows);
                format.depth = 24;
                media::Video decoder;
                frame = decoder.image(format, bitmap.compressed);
                pixels = frame.pixels.data();
                stride = frame.width;
                rows = frame.height;
            }
            const auto x = [&](int value) {
                return destination.left + MulDiv(value - decoded.frame.left,
                                                 destination.right - destination.left, width);
            };
            const auto y = [&](int value) {
                return destination.top + MulDiv(value - decoded.frame.top,
                                                destination.bottom - destination.top, height);
            };
            const int saved = SaveDC(dc);
            IntersectClipRect(dc, x(bitmap.clip.left), y(bitmap.clip.top), x(bitmap.clip.right),
                              y(bitmap.clip.bottom));
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = stride;
            info.bmiHeader.biHeight = -static_cast<LONG>(rows);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            const auto result = StretchDIBits(
                dc, x(bitmap.destination.left), y(bitmap.destination.top),
                x(bitmap.destination.right) - x(bitmap.destination.left),
                y(bitmap.destination.bottom) - y(bitmap.destination.top),
                bitmap.source.left - bitmap.bounds.left, bitmap.source.top - bitmap.bounds.top,
                bitmap.source.right - bitmap.source.left, bitmap.source.bottom - bitmap.source.top,
                pixels, &info, DIB_RGB_COLORS, SRCCOPY);
            RestoreDC(dc, saved);
            if (result == GDI_ERROR) {
                return false;
            }
        }
        return !decoded.bitmaps.empty();
    } catch (const std::exception&) {
        return false;
    }
}
}
