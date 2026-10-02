#include "xt_text.h"
#include <algorithm>
#include <array>
#include <fstream>

namespace game_assets {
namespace {
bool fail(XtText& result, const wchar_t* reason, std::size_t offset) {
    result.text.clear();
    result.status = std::wstring(reason) + L" at byte " + std::to_wstring(offset);
    return false;
}

bool append(XtText& result, std::uint32_t value, std::size_t offset) {
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        return fail(result, L"Invalid Unicode scalar", offset);
    }
    // Embedded NUL and binary controls cannot be displayed safely by native text controls.
    if ((value < 0x20 && value != 9 && value != 10 && value != 13) ||
        (value >= 0x7f && value <= 0x9f)) {
        return fail(result, L"Unsupported text control", offset);
    }
    if constexpr (sizeof(wchar_t) == 2) {
        if (value > 0xffff) {
            value -= 0x10000;
            result.text.push_back(static_cast<wchar_t>(0xd800 + (value >> 10)));
            result.text.push_back(static_cast<wchar_t>(0xdc00 + (value & 0x3ff)));
            return true;
        }
    }
    result.text.push_back(static_cast<wchar_t>(value));
    return true;
}

bool windows1252(XtText& result, std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint16_t, 32> special = {
        0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
        0x2039, 0x0152, 0,      0x017d, 0,      0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
        0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178};
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        const auto byte = bytes[at];
        const auto value =
            std::uint32_t(byte >= 0x80 && byte <= 0x9f ? special[byte - 0x80] : byte);
        if (!append(result, value, at)) {
            return false;
        }
    }
    return true;
}

bool utf8(XtText& result, std::span<const std::uint8_t> bytes, std::size_t at) {
    while (at < bytes.size()) {
        const auto start = at;
        const auto first = bytes[at++];
        std::uint32_t value = first;
        std::size_t count = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            value &= 0x1f;
            count = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            value &= 0x0f;
            count = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            value &= 7;
            count = 3;
            minimum = 0x10000;
        } else if (first >= 0x80) {
            return fail(result, L"Invalid UTF-8 leading byte", start);
        }
        if (count > bytes.size() - at) {
            return fail(result, L"Incomplete UTF-8 sequence", start);
        }
        for (std::size_t index = 0; index < count; ++index) {
            if ((bytes[at] & 0xc0) != 0x80) {
                return fail(result, L"Invalid UTF-8 continuation", at);
            }
            value = (value << 6) | (bytes[at++] & 0x3f);
        }
        if (value < minimum) {
            return fail(result, L"Overlong UTF-8 sequence", start);
        }
        if (!append(result, value, start)) {
            return false;
        }
    }
    return true;
}

std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t at, std::size_t width,
                   bool big_endian) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        const auto byte = bytes[at + (big_endian ? index : width - index - 1)];
        value = (value << 8) | byte;
    }
    return value;
}

bool unicode_words(XtText& result, std::span<const std::uint8_t> bytes, std::size_t at,
                   std::size_t width, bool big_endian) {
    if ((bytes.size() - at) % width != 0) {
        return fail(result, L"Incomplete Unicode code unit", bytes.size() - 1);
    }
    while (at < bytes.size()) {
        const auto start = at;
        auto value = word(bytes, at, width, big_endian);
        at += width;
        if (width == 2 && value >= 0xd800 && value <= 0xdbff) {
            if (at == bytes.size()) {
                return fail(result, L"Incomplete UTF-16 surrogate pair", start);
            }
            const auto low = word(bytes, at, width, big_endian);
            if (low < 0xdc00 || low > 0xdfff) {
                return fail(result, L"Invalid UTF-16 surrogate pair", at);
            }
            at += width;
            value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
        }
        if (!append(result, value, start)) {
            return false;
        }
    }
    return true;
}
}

const wchar_t* xt_encoding_name(XtEncoding encoding) {
    switch (encoding) {
        case XtEncoding::Ascii:
            return L"ASCII";
        case XtEncoding::Windows1252:
            return L"Windows-1252 (BOM-free game text)";
        case XtEncoding::Utf8:
            return L"UTF-8 BOM";
        case XtEncoding::Utf16Le:
            return L"UTF-16LE BOM";
        case XtEncoding::Utf16Be:
            return L"UTF-16BE BOM";
        case XtEncoding::Utf32Le:
            return L"UTF-32LE BOM";
        case XtEncoding::Utf32Be:
            return L"UTF-32BE BOM";
        default:
            return L"Unknown";
    }
}

XtText parse_xt_text(std::span<const std::uint8_t> bytes) {
    XtText result;
    result.file_size = bytes.size();
    const auto raw = bytes.first(std::min(bytes.size(), xt_text_raw_limit));
    result.raw.assign(raw.begin(), raw.end());
    if (bytes.size() > xt_text_limit) {
        result.status = L"XT text exceeds the 1 MiB safety limit";
        return result;
    }
    result.text.reserve(bytes.size());
    if (bytes.size() >= 4 && bytes[0] == 0xff && bytes[1] == 0xfe && bytes[2] == 0 &&
        bytes[3] == 0) {
        result.encoding = XtEncoding::Utf32Le;
        result.valid = unicode_words(result, bytes, 4, 4, false);
    } else if (bytes.size() >= 4 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0xfe &&
               bytes[3] == 0xff) {
        result.encoding = XtEncoding::Utf32Be;
        result.valid = unicode_words(result, bytes, 4, 4, true);
    } else if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        result.encoding = XtEncoding::Utf16Le;
        result.valid = unicode_words(result, bytes, 2, 2, false);
    } else if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) {
        result.encoding = XtEncoding::Utf16Be;
        result.valid = unicode_words(result, bytes, 2, 2, true);
    } else if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
        result.encoding = XtEncoding::Utf8;
        result.valid = utf8(result, bytes, 3);
    } else {
        // Original game text uses Windows-1252. A BOM is required to override that encoding.
        result.encoding =
            std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t byte) { return byte < 0x80; })
                ? XtEncoding::Ascii
                : XtEncoding::Windows1252;
        result.valid = windows1252(result, bytes);
    }
    if (result.valid) {
        result.status = L"XT text decoded";
    }
    return result;
}

XtText load_xt_text(const std::filesystem::path& path) {
    XtText result;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size < std::streampos(0)) {
        result.status = L"XT text file could not be read";
        return result;
    }
    result.file_size = static_cast<std::uint64_t>(size);
    const bool oversized = result.file_size > xt_text_limit;
    const auto read_size = oversized ? xt_text_raw_limit : std::size_t(result.file_size);
    std::vector<std::uint8_t> bytes(read_size);
    file.seekg(0);
    if (read_size && !file.read(reinterpret_cast<char*>(bytes.data()),
                                static_cast<std::streamsize>(read_size))) {
        result.status = L"XT text file changed or could not be read completely";
        return result;
    }
    if (oversized) {
        result.raw = std::move(bytes);
        result.status = L"XT text exceeds the 1 MiB safety limit";
        return result;
    }
    if (file.peek() != std::char_traits<char>::eof() || file.bad()) {
        result.status = L"XT text file changed while being read";
        return result;
    }
    return parse_xt_text(bytes);
}
}
