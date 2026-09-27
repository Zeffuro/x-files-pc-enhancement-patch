#include "browser_state.h"
#include "artwork.h"
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace saves {
namespace {
void fill(HDC dc, RECT rect, COLORREF color) {
    const auto brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void outline(HDC dc, RECT rect, COLORREF color) {
    const auto brush = CreateSolidBrush(color);
    FrameRect(dc, &rect, brush);
    DeleteObject(brush);
}

void text(HDC dc, RECT rect, const std::wstring& value, COLORREF color, UINT align = DT_LEFT) {
    auto shadow = rect;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &shadow,
              align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect,
              align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void button(HDC dc, RECT rect, const std::wstring& value, bool focused = false,
            bool enabled = true) {
    if (focused) {
        auto glow = rect;
        InflateRect(&glow, -4, -3);
        outline(dc, glow, RGB(23, 66, 89));
    }
    text(dc, rect, value,
         !enabled  ? RGB(63, 89, 103)
         : focused ? RGB(155, 216, 239)
                   : RGB(51, 129, 161),
         DT_CENTER);
}

void image(HDC dc, RECT rect, const Thumbnail& thumbnail) {
    if (thumbnail.pixels.empty()) {
        return;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = thumbnail.width;
    info.bmiHeader.biHeight = -static_cast<LONG>(thumbnail.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    const auto scale = std::max(double(rect.right - rect.left) / thumbnail.width,
                                double(rect.bottom - rect.top) / thumbnail.height);
    const auto width = static_cast<int>(thumbnail.width * scale);
    const auto height = static_cast<int>(thumbnail.height * scale);
    const auto saved = SaveDC(dc);
    IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc, rect.left + (rect.right - rect.left - width) / 2,
                  rect.top + (rect.bottom - rect.top - height) / 2, width, height, 0, 0,
                  thumbnail.width, thumbnail.height, thumbnail.pixels.data(), &info, DIB_RGB_COLORS,
                  SRCCOPY);
    RestoreDC(dc, saved);
}

void vignette(Canvas& canvas, Canvas& image_canvas, RECT rect, const Thumbnail& thumbnail,
              bool focused) {
    fill(image_canvas.dc, rect, RGB(0, 0, 0));
    image(image_canvas.dc, rect, thumbnail);
    GdiFlush();
    const double width = rect.right - rect.left, height = rect.bottom - rect.top;
    for (LONG y = rect.top; y < rect.bottom; ++y) {
        for (LONG x = rect.left; x < rect.right; ++x) {
            const auto nx = std::abs((x - rect.left + 0.5) * 2 / width - 1);
            const auto ny = std::abs((y - rect.top + 0.5) * 2 / height - 1);
            const auto radius = std::sqrt(std::sqrt(nx * nx * nx * nx + ny * ny * ny * ny));
            const auto alpha = std::clamp((0.91 - radius) * 55.0, 0.0, 1.0);
            const auto glow = focused ? std::max(0.0, 1.0 - std::abs(radius - 0.9) * 22) * 0.5 : 0;
            const auto offset = (y * 640 + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const auto blue = c == 2 ? 20 : c == 1 ? 100 : 150;
                const auto value = image_canvas.pixels[offset + c] * alpha +
                                   canvas.pixels[offset + c] * (1 - alpha);
                canvas.pixels[offset + c] =
                    static_cast<std::uint8_t>(std::min(255.0, value + blue * glow));
            }
        }
    }
}

void clear_old_slots(Canvas& canvas) {
    GdiFlush();
    for (int y = 112; y < 378; ++y) {
        for (int x = 172; x < 468; ++x) {
            const auto edge = std::min({x - 172, 467 - x, y - 112, 377 - y});
            const auto alpha = std::clamp(edge / 12.0, 0.0, 1.0);
            for (int c = 0; c < 3; ++c) {
                auto& pixel = canvas.pixels[(y * 640 + x) * 4 + c];
                pixel = static_cast<std::uint8_t>(pixel * (1 - alpha));
            }
        }
    }
    fill(canvas.dc, {190, 375, 478, 402}, RGB(0, 0, 0));
}
}

Canvas::Canvas() {
    dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 640;
    info.bmiHeader.biHeight = -480;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    bitmap =
        CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!dc || !bitmap) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (dc) {
            DeleteDC(dc);
        }
        throw std::runtime_error("Cannot create the save browser");
    }
    previous = SelectObject(dc, bitmap);
}

Canvas::~Canvas() {
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void load_browser_page(Browser& state) {
    state.preview.reset();
    state.preview_slot = -1;
    state.preview_failed = false;
    state.hover = -1;
    for (unsigned i = 0; i < slots_per_page; ++i) {
        if (state.existing) {
            state.slots[i] = {};
            const auto index = state.page * slots_per_page + i;
            if (index < state.legacy.entries.size()) {
                const auto& entry = state.legacy.entries[index];
                state.slots[i] = {0,  entry.name, entry.modified,        entry.path,
                                  {}, true,       entry.header_supported};
            }
        } else {
            state.slots[i] = read_slot(state.root, state.page * slots_per_page + i + 1);
        }
        state.thumbnails[i] = read_thumbnail(state.slots[i].thumbnail);
    }
    state.name = state.slots[state.selection].name;
    state.confirm = false;
    state.naming = false;
}

void load_browser_art(Browser& state) {
    const RECT bounds{0, 0, 640, 480};
    fill(state.background.dc, bounds, RGB(0, 0, 0));
    draw_artwork(state.background.dc, state.root, "savegameMAIN.pic", bounds);
    if (!state.saving) {
        draw_artwork(state.background.dc, state.root, "loadTOPpatch.pic", {253, 20, 387, 78});
    }
    if (draw_artwork(state.scratch.dc, state.root, "optionsmain.pic", bounds)) {
        GdiFlush();
        for (int y = 126; y < 421; ++y) {
            for (int x = 160; x < 480; ++x) {
                const auto edge = std::min({x - 160, 479 - x, y - 126, 420 - y});
                const auto alpha = std::clamp(edge / 12.0, 0.0, 1.0);
                for (int c = 0; c < 3; ++c) {
                    const auto offset = (y * 640 + x) * 4 + c;
                    state.background.pixels[offset] =
                        static_cast<std::uint8_t>(state.background.pixels[offset] * (1 - alpha) +
                                                  state.scratch.pixels[offset] * alpha);
                }
            }
        }
    } else {
        clear_old_slots(state.background);
    }
}

Thumbnail capture_scene(HDC dc, const RECT& bounds) {
    if (!dc || bounds.left < 0 || bounds.top < 0 || bounds.right > 640 || bounds.bottom > 480 ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return {};
    }
    Canvas capture;
    const auto width = 300u;
    const auto height = static_cast<unsigned>(
        MulDiv(bounds.bottom - bounds.top, width, bounds.right - bounds.left));
    if (!height || height > 480) {
        return {};
    }
    SetStretchBltMode(capture.dc, HALFTONE);
    if (!StretchBlt(capture.dc, 0, 0, width, height, dc, bounds.left, bounds.top,
                    bounds.right - bounds.left, bounds.bottom - bounds.top, SRCCOPY)) {
        return {};
    }
    GdiFlush();
    Thumbnail result{width, height, {}};
    result.pixels.reserve(width * height * 4);
    for (unsigned y = 0; y < height; ++y) {
        const auto row = capture.pixels + y * 640 * 4;
        result.pixels.insert(result.pixels.end(), row, row + width * 4);
    }
    return result;
}

void draw_browser(Browser& state) {
    const auto dc = state.output.dc;
    BitBlt(dc, 0, 0, 640, 480, state.background.dc, 0, 0, SRCCOPY);
    SetBkMode(dc, TRANSPARENT);
    if (!state.font) {
        state.font = create_browser_font(state.root, -12);
    }
    if (!state.action_font) {
        state.action_font = create_browser_font(state.root, -21);
    }
    const auto previous = SelectObject(dc, state.font);
    const auto& words = state.text;
    for (unsigned i = 0; i < slots_per_page; ++i) {
        const auto box = card_rect(i);
        const auto& slot = state.slots[i];
        const RECT preview{box.left, box.top, box.right, box.top + 82};
        const auto selected = state.focus == static_cast<int>(i);
        if (state.preview && state.preview_slot == static_cast<int>(i)) {
            const auto& frame = state.preview->frame();
            vignette(state.output, state.scratch, preview,
                     {frame.width, frame.height, frame.pixels}, selected);
        } else {
            vignette(state.output, state.scratch, preview, state.thumbnails[i], selected);
        }
        if (!slot.occupied) {
            text(dc, preview, state.existing ? L"" : words.empty,
                 selected ? RGB(160, 200, 217) : RGB(71, 112, 132), DT_CENTER);
        }
        const auto title =
            state.existing ? slot.name
                           : (slot.name.empty() ? words.slot + L" " + std::to_wstring(slot.number)
                                                : slot.name);
        text(dc, {box.left, box.top + 82, box.right, box.top + 98}, title, RGB(170, 195, 206),
             DT_CENTER);
        text(dc, {box.left, box.top + 98, box.right, box.bottom},
             slot.occupied && !slot.readable ? words.unreadable : slot.date, RGB(113, 157, 176),
             DT_CENTER);
    }
    const auto pages = state.existing
                           ? std::max<std::size_t>(1, (state.legacy.entries.size() + 5) / 6)
                           : slot_pages;
    button(dc, control_rect(6), L"< " + words.previous, state.focus == 6);
    button(dc, control_rect(7), words.next + L" >", state.focus == 7);
    text(dc, {250, 78, 390, 94}, std::to_wstring(state.page + 1) + L" / " + std::to_wstring(pages),
         RGB(85, 135, 158), DT_CENTER);
    if (!state.saving) {
        button(dc, control_rect(8), state.existing ? words.numbered : words.existing,
               state.focus == 8);
    } else {
        text(dc, {104, 373, 160, 395}, words.name, RGB(82, 135, 157));
        const auto field = control_rect(9);
        fill(dc, field, RGB(0, 6, 10));
        if (state.focus == 9) {
            outline(dc, field, RGB(41, 95, 120));
        }
        auto inset = field;
        InflateRect(&inset, -5, 0);
        text(dc, inset, state.name, RGB(178, 207, 217));
    }
    if (!state.confirm) {
        text(dc, {70, 439, 570, 466}, state.status, RGB(156, 197, 211), DT_CENTER);
    }
    const auto draw_action = [&](int item, const std::wstring& label) {
        SelectObject(dc, state.action_font);
        text(dc, control_rect(item), label,
             !control_enabled(state, item) ? RGB(63, 89, 103)
             : state.focus == item         ? RGB(155, 216, 239)
                                           : RGB(51, 129, 161),
             DT_CENTER);
        SelectObject(dc, state.font);
        if (state.focus == item) {
            auto line = control_rect(item);
            line.top = line.bottom - 1;
            fill(dc, line, RGB(62, 124, 155));
        }
    };
    draw_action(10, words.cancel);
    draw_action(11, state.deleting ? words.remove : state.saving ? words.save : words.load);
    if (control_enabled(state, 12)) {
        button(dc, control_rect(12), words.remove, state.focus == 12);
    }
    if (state.confirm) {
        // Keep the native action positions while making the pending action explicit.
        fill(dc, {92, 334, 548, 395}, RGB(1, 10, 17));
        outline(dc, {92, 334, 548, 395}, RGB(39, 107, 140));
        auto bounds = RECT{104, 342, 536, 388};
        SetTextColor(dc, RGB(174, 216, 233));
        DrawTextW(dc, state.status.c_str(), -1, &bounds, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    }
    if (state.keyboard) {
        fill(dc, {99, 222, 541, 409}, RGB(0, 8, 15));
        outline(dc, {99, 222, 541, 409}, RGB(31, 98, 132));
        text(dc, {112, 229, 528, 250}, state.name + L"|", RGB(182, 217, 231));
        const std::array<std::wstring, 4> actions{words.space, words.backspace, words.clear,
                                                  words.done};
        for (unsigned key = 0; key < name_key_count; ++key) {
            button(dc, name_key_rect(key),
                   key < 40 ? std::wstring(1, name_keys[key]) : actions[key - 40],
                   state.key == key);
        }
    }
    SelectObject(dc, previous);
    GdiFlush();
}
}
