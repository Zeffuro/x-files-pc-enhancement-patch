#include "page.h"
#include "enhancements/ui/game_style.h"
#include <algorithm>
#include <stdexcept>

namespace transcript {
namespace {
constexpr RECT full{0, 0, 640, 480};

void fill(HDC dc, RECT bounds, COLORREF color) {
    const auto brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

void text(HDC dc, RECT bounds, const std::wstring& value, COLORREF color, UINT flags) {
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &bounds, flags | DT_NOPREFIX);
}

void button(HDC dc, RECT bounds, const wchar_t* label, bool enabled = true) {
    text(dc, bounds, label, enabled ? enhancements::game_blue : RGB(63, 89, 103),
         DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

BOOL CALLBACK language(HMODULE, LPCWSTR, LPCWSTR, WORD found, LONG_PTR context) {
    *reinterpret_cast<LANGID*>(context) = found;
    return FALSE;
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
        throw std::runtime_error("Cannot create the dialogue transcript");
    }
    previous_ = SelectObject(dc_, bitmap_);
    font_ = enhancements::create_game_font(-16);
    heading_ = enhancements::create_game_font(-23);
}

Page::~Page() {
    SelectObject(dc_, previous_);
    DeleteObject(font_);
    DeleteObject(heading_);
    DeleteObject(bitmap_);
    DeleteDC(dc_);
}

void Page::draw(HDC background, const std::vector<std::wstring>& entries, const std::wstring& note,
                std::size_t page) {
    if (background) {
        BitBlt(dc_, 0, 0, 640, 480, background, 0, 0, SRCCOPY);
    } else {
        fill(dc_, full, RGB(0, 0, 0));
    }
    fill(dc_, {24, 20, 616, 466}, RGB(0, 0, 0));
    const auto border = CreateSolidBrush(RGB(23, 66, 89));
    const RECT panel{24, 20, 616, 466};
    FrameRect(dc_, &panel, border);
    DeleteObject(border);
    SetBkMode(dc_, TRANSPARENT);
    const auto old = SelectObject(dc_, heading_);
    text(dc_, {40, 31, 600, 66}, L"Dialogue transcript", enhancements::game_highlight,
         DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc_, font_);
    LONG content_top = 74;
    if (!note.empty()) {
        RECT bounds{40, content_top, 600, content_top};
        DrawTextW(dc_, note.c_str(), static_cast<int>(note.size()), &bounds,
                  DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        text(dc_, bounds, note, enhancements::game_blue, DT_WORDBREAK);
        content_top = bounds.bottom + 8;
    }

    // Paginate measured lines so every recorded entry remains reachable.
    struct Line {
        std::wstring value;
        bool entry_end = false;
    };

    std::vector<Line> lines;
    for (const auto& entry : entries) {
        std::wstring line;
        for (const auto character : entry) {
            if (character == L'\r') {
                continue;
            }
            if (character == L'\n') {
                lines.push_back({line});
                line.clear();
                continue;
            }
            auto candidate = line + character;
            SIZE size{};
            GetTextExtentPoint32W(dc_, candidate.c_str(), static_cast<int>(candidate.size()),
                                  &size);
            if (!line.empty() && size.cx > 548) {
                const auto space = line.find_last_of(L' ');
                if (space != std::wstring::npos && space > 0) {
                    lines.push_back({line.substr(0, space)});
                    line.erase(0, space + 1);
                } else {
                    lines.push_back({line});
                    line.clear();
                }
            }
            line += character;
        }
        lines.push_back({line, true});
    }
    if (entries.empty()) {
        lines.push_back({L"No dialogue recorded yet. Continue playing to add lines."});
    }
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc_, &metrics);
    const auto height = std::max<LONG>(16, metrics.tmHeight);
    starts_ = {0};
    LONG y = content_top;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (y + height > 412 && index > starts_.back()) {
            starts_.push_back(index);
            y = content_top;
        }
        y += height + (lines[index].entry_end ? 2 : 0);
    }
    page = std::min(page, starts_.size() - 1);
    y = content_top;
    const auto end = page + 1 < starts_.size() ? starts_[page + 1] : lines.size();
    for (std::size_t i = starts_[page]; i < end; ++i) {
        text(dc_, {40, y, 600, y + height}, lines[i].value, enhancements::game_highlight,
             DT_SINGLELINE);
        y += height + (lines[i].entry_end ? 2 : 0);
    }
    SelectObject(dc_, heading_);
    button(dc_, previous_button, L"Previous", page > 0);
    button(dc_, next_button, L"Next", page + 1 < starts_.size());
    button(dc_, close_button, L"Done");
    SelectObject(dc_, font_);
    text(dc_, {326, 423, 454, 463},
         std::to_wstring(page + 1) + L" / " + std::to_wstring(starts_.size()),
         enhancements::game_blue, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    SelectObject(dc_, old);
}

std::wstring edition_note(const std::filesystem::path& root) {
    LANGID id = 0;
    const auto module =
        LoadLibraryExW((root / L"XFILESE.DLL").c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (module) {
        EnumResourceLanguagesW(module, RT_STRING, MAKEINTRESOURCEW(1), language,
                               reinterpret_cast<LONG_PTR>(&id));
        FreeLibrary(module);
    }
    if (id && PRIMARYLANGID(id) != LANG_ENGLISH) {
        return L"Dialogue captions may be unavailable in this edition.";
    }
    return {};
}
}
