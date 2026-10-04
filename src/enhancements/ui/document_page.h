#pragma once
#include "enhancements/document_text.h"

namespace enhancements::documents {
inline constexpr RECT smaller_button{42, 423, 135, 461};
inline constexpr RECT larger_button{142, 423, 235, 461};
inline constexpr RECT up_button{252, 423, 318, 461};
inline constexpr RECT down_button{325, 423, 407, 461};
inline constexpr RECT done_button{476, 423, 599, 461};

class Page {
public:
    Page();
    ~Page();
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    HDC dc() const {
        return dc_;
    }

    void draw(HDC background, const Document* document, int size, int scroll);

    int scroll() const {
        return scroll_;
    }

    int maximum() const {
        return maximum_;
    }

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    HFONT heading_ = nullptr;
    HFONT body_ = nullptr;
    int size_ = 0;
    int scroll_ = 0;
    int maximum_ = 0;
    int line_height_ = 0;
    std::wstring text_;
    std::vector<std::wstring> lines_;
};
}
