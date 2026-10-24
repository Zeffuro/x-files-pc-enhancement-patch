#include "enhancements/ui/quick_menu_surface.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 640, height = 480;
constexpr RECT native_button{501, 0, 640, 34};
constexpr std::array labels{L"Save", L"Load", L"Transcript", L"Tweaks", L"Menu"};
constexpr std::array<bool, 5> enabled{true, true, true, true, true};

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

class Canvas {
    HDC dc_ = CreateCompatibleDC(nullptr);
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    std::uint32_t* pixels_ = nullptr;

public:
    Canvas() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels_),
                                   nullptr, 0);
        require(dc_ && bitmap_ && pixels_, "Cannot create label comparison canvas");
        previous_ = SelectObject(dc_, bitmap_);
        PatBlt(dc_, 0, 0, width, height, BLACKNESS);
    }

    ~Canvas() {
        SelectObject(dc_, previous_);
        DeleteObject(bitmap_);
        DeleteDC(dc_);
    }

    HDC dc() const {
        return dc_;
    }

    void capture(HDC source) {
        require(BitBlt(dc_, 0, 0, width, height, source, 0, 0, SRCCOPY) != 0,
                "Cannot copy label comparison pixels");
    }

    bool same_labels(const Canvas& other) const {
        GdiFlush();
        return std::memcmp(pixels_ + 30 * width, other.pixels_ + 30 * width,
                           20 * width * sizeof(*pixels_)) == 0;
    }

    void save(const std::filesystem::path& path) const {
        GdiFlush();
        constexpr int left = 440, cropped_width = width - left, cropped_height = 56;
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = cropped_width;
        info.biHeight = -cropped_height;
        info.biPlanes = 1;
        info.biBitCount = 32;
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + cropped_width * cropped_height * 4;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&file), sizeof(file));
        output.write(reinterpret_cast<const char*>(&info), sizeof(info));
        for (int y = 0; y < cropped_height; ++y) {
            output.write(reinterpret_cast<const char*>(pixels_ + y * width + left),
                         cropped_width * 4);
        }
        require(static_cast<bool>(output), "Cannot save localized toolbar preview");
    }
};
}

int wmain(int argc, wchar_t** argv) {
    try {
        Canvas background, expected, actual;
        enhancements::quick_menu::Surface surface;
        unsigned cases = 0;
        for (int index = 0; index < ui::language_count; ++index) {
            const auto language = static_cast<ui::Language>(index);
            const bool japanese = language == ui::Language::Japanese;
            const auto font = CreateFontW(
                -9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                japanese ? SHIFTJIS_CHARSET : ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY, DEFAULT_PITCH, japanese ? L"MS Gothic" : L"Arial");
            require(font != nullptr, "Cannot create reference label font");
            for (const bool compact : {false, true}) {
                for (unsigned item = 0; item < labels.size(); ++item) {
                    auto visible = enabled;
                    if (compact) {
                        visible.fill(false);
                        visible[item] = true;
                    }
                    expected.capture(surface.draw(background.dc(), native_button, 100, -1, enabled,
                                                  visible, language));
                    const auto bounds =
                        enhancements::quick_menu::Surface::button(native_button, item, visible);
                    RECT label{std::max<LONG>(0, bounds.left - 35), bounds.bottom + 2,
                               std::min<LONG>(width, bounds.right + 35), bounds.bottom + 22};
                    const auto text = ui::translate(labels[item], language);
                    const auto previous = SelectObject(expected.dc(), font);
                    SetBkMode(expected.dc(), TRANSPARENT);
                    SetTextColor(expected.dc(), RGB(155, 216, 239));
                    SIZE size{};
                    GetTextExtentPoint32W(expected.dc(), text, static_cast<int>(std::wcslen(text)),
                                          &size);
                    require(size.cx <= label.right - label.left && size.cy <= 20,
                            "Translated toolbar label does not fit its available bounds");
                    require(DrawTextW(expected.dc(), text, -1, &label,
                                      DT_CENTER | DT_SINGLELINE | DT_NOPREFIX) != 0,
                            "Reference label failed to draw");
                    SelectObject(expected.dc(), previous);
                    actual.capture(surface.draw(background.dc(), native_button, 100, item, enabled,
                                                visible, language));
                    require(actual.same_labels(expected),
                            "Toolbar pixels do not match the selected language label");
                    if (argc == 2 && language == ui::Language::Russian && item == 1 && !compact) {
                        actual.save(argv[1]);
                    }
                    ++cases;
                }
            }
            DeleteObject(font);
        }
        std::cout << cases << " localized toolbar label and layout comparisons passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
