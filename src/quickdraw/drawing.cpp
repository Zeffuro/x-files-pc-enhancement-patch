#include "world.h"
#include "regions.h"
#include <intrin.h>

namespace {

using namespace quickdraw;

Color basic_color(std::int32_t color) {
    switch (color) {
        case 33:
            return {0, 0, 0};
        case 30:
            return {65535, 65535, 65535};
        case 205:
            return {65535, 0, 0};
        case 341:
            return {0, 65535, 0};
        case 409:
            return {0, 0, 65535};
        case 273:
            return {0, 65535, 65535};
        case 137:
            return {65535, 0, 65535};
        case 69:
            return {65535, 65535, 0};
        default:
            unsupported(Selector::ForeColor, "QuickDraw: unsupported color index", 0);
    }
}

void __cdecl foreground(std::int32_t color) {
    auto& port = drawing_port();
    port.foreground = basic_color(color);
    port.foreground_color = color;
}

void __cdecl background(std::int32_t color) {
    auto& port = drawing_port();
    port.background = basic_color(color);
    port.background_color = color;
}

void __cdecl rgb_foreground(const Color* color) {
    if (color) {
        drawing_port().foreground = *color;
    }
}

void __cdecl rgb_background(const Color* color) {
    if (color) {
        drawing_port().background = *color;
    }
}

void __cdecl op_color(const Color* color) {
    if (color) {
        operation_color() = *color;
    }
}

void fill(const Rect* rectangle, Color color) {
    auto& port = drawing_port();
    const auto dc = port_dc(&port);
    if (!dc || !rectangle) {
        unsupported(Selector::PaintRect, "QuickDraw: drawing requires an offscreen port", 0);
    }

    const RECT area{rectangle->left, rectangle->top, rectangle->right, rectangle->bottom};
    const auto brush = CreateSolidBrush(RGB(color.red >> 8, color.green >> 8, color.blue >> 8));
    const bool painted = brush && FillRect(dc, &area, brush);
    if (brush) {
        DeleteObject(brush);
    }
    if (!painted) {
        unsupported(Selector::PaintRect, "QuickDraw: rectangle fill failed", 0);
    }
    GdiFlush();
}

void __cdecl paint(const Rect* rectangle) {
    const auto procedures = drawing_port().procedures;
    const auto callback =
        procedures && procedures->rectangle ? procedures->rectangle : standard_rectangle;
    callback(Verb::Paint, rectangle);
}

void __cdecl erase(const Rect* rectangle) {
    const auto procedures = drawing_port().procedures;
    const auto callback =
        procedures && procedures->rectangle ? procedures->rectangle : standard_rectangle;
    callback(Verb::Erase, rectangle);
}

void __cdecl clip(const Rect* rectangle) {
    auto& port = drawing_port();
    const auto dc = port_dc(&port);
    if (!dc || !rectangle || !port.clip_region) {
        unsupported(Selector::ClipRect, "ClipRect: unknown drawing port", 0);
    }

    const auto region =
        CreateRectRgn(rectangle->left - port.bounds.left, rectangle->top - port.bounds.top,
                      rectangle->right - port.bounds.left, rectangle->bottom - port.bounds.top);
    if (!region) {
        unsupported(Selector::ClipRect, "ClipRect: region allocation failed", 0);
    }
    const auto result = SelectClipRgn(dc, region);
    DeleteObject(region);
    if (result == ERROR) {
        unsupported(Selector::ClipRect, "ClipRect: setting the clip failed", 0);
    }
    set_region_rect(port.clip_region, *rectangle);
}

void __cdecl get_clip(RegionHandle output) {
    copy_region_data(drawing_port().clip_region, output);
}

void __cdecl set_clip(RegionHandle input) {
    auto& port = drawing_port();
    const auto dc = port_dc(&port);
    if (!dc || !port.clip_region) {
        unsupported(Selector::SetClip, "SetClip: missing drawing port", 0);
    }
    auto region = native_region(input);
    POINT origin{};
    GetViewportOrgEx(dc, &origin);
    if (OffsetRgn(region.get(), origin.x, origin.y) == ERROR ||
        SelectClipRgn(dc, region.get()) == ERROR) {
        unsupported(Selector::SetClip, "SetClip: cannot apply region", 0);
    }
    copy_region_data(input, port.clip_region);
}

}

namespace quickdraw {

void __cdecl standard_rectangle(Verb verb, const Rect* rectangle) {
    trace_call(static_cast<std::uint32_t>(Selector::SetStdCProcs), "StdRect",
               reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    auto& port = drawing_port();
    if (verb == Verb::Erase && !port.background_pattern) {
        fill(rectangle, port.background);
        return;
    }
    if (verb == Verb::Paint && !port.pen_pattern && port.pen_mode == 0) {
        fill(rectangle, port.foreground);
        return;
    }
    unsupported(Selector::SetStdCProcs, "StdRect: unsupported drawing operation or pattern", 0);
}

}

Entry drawing_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::ClipRect, clip),
        bind_entry(Selector::GetClip, get_clip),
        bind_entry(Selector::SetClip, set_clip),
        bind_entry(Selector::ForeColor, foreground),
        bind_entry(Selector::BackColor, background),
        bind_entry(Selector::PaintRect, paint),
        bind_entry(Selector::EraseRect, erase),
        bind_entry(Selector::RGBForeColor, rgb_foreground),
        bind_entry(Selector::RGBBackColor, rgb_background),
        bind_entry(Selector::OpColor, op_color),
    };
    if (const auto entry = find_entry(selector, entries)) {
        return entry;
    }
    return procedures_entry(selector);
}
