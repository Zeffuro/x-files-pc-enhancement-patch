#include "font_preview.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <span>
#include <stdexcept>
#include <vector>

namespace devtools {
namespace {
using Bytes = std::span<const std::uint8_t>;

std::uint16_t word(Bytes bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 2) {
        throw std::runtime_error("Font header is incomplete");
    }
    return static_cast<std::uint16_t>((bytes[at] << 8) | bytes[at + 1]);
}

std::uint32_t dword(Bytes bytes, std::size_t at) {
    return (std::uint32_t(word(bytes, at)) << 16) | word(bytes, at + 2);
}

Bytes section(Bytes bytes, std::size_t at, std::size_t length) {
    if (at > bytes.size() || length > bytes.size() - at) {
        throw std::runtime_error("Font table is incomplete");
    }
    return bytes.subspan(at, length);
}

std::wstring face_name(Bytes bytes) {
    const auto version = dword(bytes, 0);
    if (version != 0x00010000 && version != 0x74727565) {
        throw std::runtime_error("Preview requires a TrueType TTR or TTF font");
    }
    const auto count = word(bytes, 4);
    if (!count || count > 256) {
        throw std::runtime_error("Font table directory is invalid");
    }
    section(bytes, 12, std::size_t(count) * 16);
    Bytes names;
    for (std::size_t i = 0; i < count; ++i) {
        const auto at = 12 + i * 16;
        const auto table = section(bytes, dword(bytes, at + 8), dword(bytes, at + 12));
        if (dword(bytes, at) == 0x6e616d65) {
            names = table;
        }
    }
    if (names.empty() || word(names, 0) > 1) {
        throw std::runtime_error("Font has no supported name table");
    }
    const auto records = word(names, 2);
    const auto strings = word(names, 4);
    section(names, 6, std::size_t(records) * 12);
    if (strings < 6 + std::size_t(records) * 12) {
        throw std::runtime_error("Font name table is invalid");
    }
    std::wstring best;
    unsigned best_score = 0;
    for (std::size_t i = 0; i < records; ++i) {
        const auto at = 6 + i * 12;
        const auto platform = word(names, at);
        const auto encoding = word(names, at + 2);
        const auto id = word(names, at + 6);
        const auto text =
            section(names, std::size_t(strings) + word(names, at + 10), word(names, at + 8));
        if ((platform != 0 && !(platform == 3 && (encoding == 1 || encoding == 10))) ||
            (id != 1 && id != 4) || text.empty() || text.size() % 2 ||
            text.size() / 2 >= LF_FACESIZE) {
            continue;
        }
        std::wstring candidate;
        for (std::size_t ch = 0; ch < text.size(); ch += 2) {
            const auto value = word(text, ch);
            if (value < 32 || (value >= 0xd800 && value <= 0xdfff)) {
                candidate.clear();
                break;
            }
            candidate += static_cast<wchar_t>(value);
        }
        const unsigned score = (id == 4 ? 4U : 0U) + (platform == 3 ? 2U : 0U) +
                               (word(names, at + 4) == 0x0409 ? 1U : 0U);
        if (!candidate.empty() && (best.empty() || score > best_score)) {
            best = std::move(candidate);
            best_score = score;
        }
    }
    if (best.empty()) {
        throw std::runtime_error("Font has no usable Unicode face name");
    }
    return best;
}

std::vector<std::uint8_t> read_font(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input ? input.tellg() : std::streampos(-1);
    if (size < std::streampos(12) || size > std::streampos(16 * 1024 * 1024)) {
        throw std::runtime_error("Cannot read font or font exceeds the 16 MiB limit");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())) ||
        input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Font changed while being read");
    }
    return bytes;
}

bool matches(HDC dc, HFONT font, Bytes bytes) {
    const auto previous = SelectObject(dc, font);
    const auto size = GetFontData(dc, 0, 0, nullptr, 0);
    bool result = false;
    if (size == bytes.size()) {
        std::vector<std::uint8_t> actual(size);
        result = GetFontData(dc, 0, 0, actual.data(), size) == size &&
                 std::equal(actual.begin(), actual.end(), bytes.begin());
    }
    SelectObject(dc, previous);
    return result;
}
}

FontPreview::~FontPreview() {
    clear();
}

void FontPreview::clear() {
    for (auto& font : fonts_) {
        if (font) {
            DeleteObject(font);
            font = nullptr;
        }
    }
    if (resource_) {
        RemoveFontMemResourceEx(resource_);
        resource_ = nullptr;
    }
    description_.clear();
}

void FontPreview::load(const std::filesystem::path& path) {
    clear();
    try {
        auto bytes = read_font(path);
        const auto face = face_name(bytes);
        DWORD count = 0;
        resource_ =
            AddFontMemResourceEx(bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, &count);
        if (!resource_ || count != 1) {
            throw std::runtime_error("Cannot load this single-face TrueType font");
        }
        const std::array heights{36, 24, 16};
        for (std::size_t i = 0; i < fonts_.size(); ++i) {
            fonts_[i] = CreateFontW(-heights[i], 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                                    ANTIALIASED_QUALITY, DEFAULT_PITCH, face.c_str());
            if (!fonts_[i]) {
                throw std::runtime_error("Cannot create the font preview");
            }
        }
        const auto dc = CreateCompatibleDC(nullptr);
        if (!dc) {
            throw std::runtime_error("Cannot create the font preview surface");
        }
        bool verified = false;
        try {
            verified = matches(dc, fonts_[0], bytes);
        } catch (...) {
            DeleteDC(dc);
            throw;
        }
        DeleteDC(dc);
        if (!verified) {
            throw std::runtime_error("Font preview could not select the actual asset font");
        }
        description_ = face + L"\r\nTrueType font. Sample sizes: 36, 24 and 16 pixels.";
    } catch (const std::exception& error) {
        clear();
        const std::string message = error.what();
        description_ = L"Font preview unavailable: " + std::wstring(message.begin(), message.end());
    }
}

void FontPreview::paint(HDC dc, RECT bounds) const {
    const auto saved = SaveDC(dc);
    if (!saved) {
        return;
    }
    IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    FillRect(dc, &bounds, GetSysColorBrush(COLOR_WINDOW));
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    SetBkMode(dc, TRANSPARENT);
    bounds.left += 16;
    bounds.right -= 16;
    bounds.top += 12;
    if (!loaded()) {
        DrawTextW(dc, description_.c_str(), -1, &bounds, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    } else {
        const std::array samples{L"The X-Files\r\nThe quick brown fox jumps over the lazy dog.",
                                 L"ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\nabcdefghijklmnopqrstuvwxyz",
                                 L"0123456789  !?.,:; ' \" () [] + - /\r\nThe truth is out there."};
        for (std::size_t i = 0; i < fonts_.size(); ++i) {
            SelectObject(dc, fonts_[i]);
            RECT measured = bounds;
            DrawTextW(dc, samples[i], -1, &measured,
                      DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            DrawTextW(dc, samples[i], -1, &bounds, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            bounds.top = measured.bottom + 20;
        }
    }
    RestoreDC(dc, saved);
}
}
