#include "devtools/database/font_preview.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    require(bool(output), "cannot write font fixture");
}

std::vector<std::uint8_t> system_font(HDC dc) {
    const auto font = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH, L"Arial");
    if (!font) {
        throw std::runtime_error("cannot create fixture font");
    }
    const auto previous = SelectObject(dc, font);
    const auto size = GetFontData(dc, 0, 0, nullptr, 0);
    if (size == GDI_ERROR || size > 16 * 1024 * 1024) {
        SelectObject(dc, previous);
        DeleteObject(font);
        throw std::runtime_error("cannot read fixture TrueType font");
    }
    std::vector<std::uint8_t> bytes(size);
    const auto read = GetFontData(dc, 0, 0, bytes.data(), size);
    SelectObject(dc, previous);
    DeleteObject(font);
    require(read == size, "fixture font short read");
    return bytes;
}
}

int main() {
    try {
        const auto directory = std::filesystem::temp_directory_path() /
                               (L"xfiles-font-preview-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directory(directory);
        const auto fixture = directory / L"sample.ttr";
        const auto dc = CreateCompatibleDC(nullptr);
        if (!dc) {
            throw std::runtime_error("cannot create preview surface");
        }
        const auto bytes = system_font(dc);
        const auto baseline = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        write(fixture, bytes);
        for (unsigned i = 0; i < 20; ++i) {
            devtools::FontPreview preview;
            preview.load(fixture);
            require(preview.loaded(), "valid TrueType font not loaded");
            require(preview.description().find(L"TrueType") != std::wstring::npos,
                    "font description missing");
            preview.clear();
            require(!preview.loaded() && preview.description().empty(), "clear kept font state");
        }
        require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == baseline,
                "font load and removal leaked GDI objects");
        std::vector<std::vector<std::uint8_t>> invalid{
            {}, {0, 1, 0, 0}, std::vector<std::uint8_t>(12, 0)};
        auto truncated = bytes;
        truncated.resize(100);
        invalid.push_back(truncated);
        auto outside = bytes;
        outside[20] = 0xff;
        invalid.push_back(outside);
        auto unsupported = bytes;
        unsupported[0] = 'M';
        unsupported[1] = 'Z';
        invalid.push_back(unsupported);
        devtools::FontPreview preview;
        for (const auto& malformed : invalid) {
            write(fixture, bytes);
            preview.load(fixture);
            require(preview.loaded(), "valid reload failed");
            write(fixture, malformed);
            preview.load(fixture);
            require(!preview.loaded(), "malformed font accepted");
            require(preview.description().find(L"unavailable") != std::wstring::npos,
                    "malformed font error missing");
        }
        preview.load(directory / L"missing.ttf");
        require(!preview.loaded(), "missing font accepted");
        require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == baseline,
                "failed replacement leaked GDI objects");
        DeleteDC(dc);
        std::filesystem::remove(fixture);
        std::filesystem::remove(directory);
        std::cout << "Font loading, malformed replacement and lifetime checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
