#pragma once
#include <windows.h>
#include <array>

namespace enhancements::quick_menu {
class Surface {
public:
    Surface();
    ~Surface();
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    HDC draw(HDC background, RECT native_button, int progress, int hover,
             const std::array<bool, 5>& enabled, const std::array<bool, 5>& visible);
    static RECT toggle(RECT native_button);
    static RECT reveal(RECT native_button);
    static RECT button(RECT native_button, unsigned index,
                       const std::array<bool, 5>& visible = {true, true, true, true, true});
    static RECT region(RECT native_button, const std::array<bool, 5>& visible);

private:
    HDC dc_ = nullptr, icon_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr, icon_bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr, icon_previous_ = nullptr;
    HFONT font_ = nullptr;
    unsigned* pixels_ = nullptr;
    void icon(unsigned index, int x, int y, COLORREF color);
};
}
