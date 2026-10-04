#include "document_page.h"
#include "game_style.h"
#include <algorithm>
#include <stdexcept>

namespace enhancements::documents {
namespace {
constexpr COLORREF paper = RGB(230, 228, 214);
constexpr COLORREF ink = RGB(45, 48, 47);
constexpr COLORREF frame = RGB(98, 103, 100);

void fill(HDC dc, RECT bounds, COLORREF color) {
    const auto brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

void text(HDC dc, RECT bounds, const std::wstring& value, UINT flags) {
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &bounds, flags | DT_NOPREFIX);
}

void button(HDC dc, RECT bounds, const wchar_t* label) {
    fill(dc, bounds, RGB(213, 215, 205));
    text(dc, bounds, label, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

std::vector<std::wstring> wrap(HDC dc, std::wstring_view value, int width) {
    std::vector<std::wstring> lines;
    std::wstring paragraph;
    const auto flush = [&] {
        if (paragraph.empty()) {
            lines.emplace_back();
        }
        std::size_t at = 0;
        while (at < paragraph.size()) {
            int fit = 0;
            SIZE measured{};
            GetTextExtentExPointW(dc, paragraph.data() + at,
                                  static_cast<int>(paragraph.size() - at), width, &fit, nullptr,
                                  &measured);
            auto count = std::max<std::size_t>(1, static_cast<std::size_t>(fit));
            if (at + count < paragraph.size()) {
                const auto space = paragraph.find_last_of(L" ", at + count - 1);
                if (space != std::wstring::npos && space >= at) {
                    count = space + 1 - at;
                }
            }
            lines.push_back(paragraph.substr(at, count));
            at += count;
        }
        paragraph.clear();
    };
    for (const auto character : value) {
        if (character == L'\n') {
            flush();
        } else if (character == L'\t') {
            paragraph += L"    ";
        } else if (character != L'\r') {
            paragraph += character;
        }
    }
    flush();
    return lines;
}
}

Page::Page() {
    dc_ = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 640;
    info.bmiHeader.biHeight = -480;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc_ || !bitmap_) {
        if (bitmap_) {
            DeleteObject(bitmap_);
        }
        if (dc_) {
            DeleteDC(dc_);
        }
        throw std::runtime_error("Cannot create the document view");
    }
    previous_ = SelectObject(dc_, bitmap_);
    heading_ = create_game_font(-20);
}

Page::~Page() {
    SelectObject(dc_, previous_);
    DeleteObject(body_);
    DeleteObject(heading_);
    DeleteObject(bitmap_);
    DeleteDC(dc_);
}

void Page::draw(HDC background, const Document* document, int size, int scroll) {
    if (background) {
        BitBlt(dc_, 0, 0, 640, 480, background, 0, 0, SRCCOPY);
    } else {
        fill(dc_, {0, 0, 640, 480}, RGB(0, 0, 0));
    }
    SetBkMode(dc_, TRANSPARENT);
    SetTextColor(dc_, ink);
    const auto old = SelectObject(dc_, heading_);
    if (!document) {
        SelectObject(dc_, old);
        return;
    }
    const RECT panel{24, 18, 616, 466};
    fill(dc_, panel, paper);
    const auto border = CreateSolidBrush(frame);
    FrameRect(dc_, &panel, border);
    DeleteObject(border);
    text(dc_, {42, 30, 597, 63}, document->title, DT_SINGLELINE | DT_VCENTER);
    fill(dc_, {42, 68, 599, 69}, frame);
    size = std::clamp(size, 16, 28);
    const bool changed = size != size_ || document->text != text_;
    if (size != size_) {
        DeleteObject(body_);
        body_ = create_game_font(-size);
        size_ = size;
    }
    SelectObject(dc_, body_);
    if (changed) {
        text_ = document->text;
        lines_ = wrap(dc_, text_, 539);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc_, &metrics);
        line_height_ = std::max(16L, metrics.tmHeight) + 2;
    }
    constexpr int visible = 326;
    maximum_ = std::max(0, static_cast<int>(lines_.size()) * line_height_ - visible);
    scroll_ = std::clamp(scroll, 0, maximum_);
    const auto saved = SaveDC(dc_);
    IntersectClipRect(dc_, 42, 80, 581, 406);
    auto y = 80 - scroll_ % line_height_;
    for (std::size_t index = scroll_ / line_height_; index < lines_.size() && y < 406; ++index) {
        text(dc_, {42, y, 581, y + line_height_}, lines_[index], DT_SINGLELINE);
        y += line_height_;
    }
    RestoreDC(dc_, saved);
    if (maximum_) {
        fill(dc_, {592, 80, 599, 406}, RGB(206, 207, 195));
        const auto thumb = std::max(18, visible * visible / (visible + maximum_));
        const auto top = 80 + (visible - thumb) * scroll_ / maximum_;
        fill(dc_, {592, top, 599, top + thumb}, frame);
    }
    SelectObject(dc_, heading_);
    button(dc_, smaller_button, L"A -");
    button(dc_, larger_button, L"A +");
    button(dc_, up_button, L"Up");
    button(dc_, down_button, L"Down");
    button(dc_, done_button, L"Done");
    SelectObject(dc_, old);
}
}
