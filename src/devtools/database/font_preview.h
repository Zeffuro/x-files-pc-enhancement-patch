#pragma once
#include <windows.h>
#include <array>
#include <filesystem>
#include <string>

namespace devtools {
class FontPreview {
public:
    FontPreview() = default;
    ~FontPreview();
    FontPreview(const FontPreview&) = delete;
    FontPreview& operator=(const FontPreview&) = delete;

    void load(const std::filesystem::path& path);
    void clear();

    bool loaded() const {
        return resource_ != nullptr;
    }

    const std::wstring& description() const {
        return description_;
    }

    void paint(HDC dc, RECT bounds) const;

private:
    HANDLE resource_ = nullptr;
    std::array<HFONT, 3> fonts_{};
    std::wstring description_;
};
}
