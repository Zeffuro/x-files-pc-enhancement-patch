#include "native_render.h"
#include "render_internal.h"
#include "caption_surface.h"
#include "enhancements/game_ui.h"
#include "quickdraw/world.h"
#include "runtime.h"
#include <array>
#include <cstring>

namespace native_game {
namespace {
using Update = int(__stdcall*)(void*, void*);
using Draw = int(__stdcall*)(void*, void*, void*);
using Invalidate = int(__stdcall*)(void*, Rectangle*);
using Transfer = int(__stdcall*)(void*, void*, Rectangle*, Rectangle*, void*, int);
Update original_update = nullptr;
Draw original_draw = nullptr;
Transfer original_transfer = nullptr;
std::byte* image = nullptr;
const Profile* profile = nullptr;
std::array<std::byte, 5> original_call{};
bool attached = false;
thread_local CanvasSource canvas_source = nullptr;
thread_local CanvasSource canvas_overlay = nullptr;
thread_local HDC presenting = nullptr;
thread_local bool substituting = false;
thread_local bool source_reported = false;

template <typename T> bool read(const void* address, T& result) {
    SIZE_T size = 0;
    return address &&
           ReadProcessMemory(GetCurrentProcess(), address, &result, sizeof(result), &size) &&
           size == sizeof(result);
}

std::byte* current_canvas() {
    const auto view = enhancements::game::current_view();
    return profile && view ? reinterpret_cast<std::byte*>(view) + profile->canvas : nullptr;
}

bool replace(void* address, const void* data, std::size_t size) {
    DWORD protection = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &protection)) {
        return false;
    }
    std::memcpy(address, data, size);
    DWORD unused = 0;
    VirtualProtect(address, size, protection, &unused);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    return true;
}

struct Point {
    void* vtable;
    POINT value;
};

struct Positioned {
    std::byte* drawable;
    Point position;
    void* effect;
};

template <typename Action> void credits(void* group, Action action) {
    const auto view = enhancements::game::current_view();
    if (!view || group != reinterpret_cast<std::byte*>(view) + 0x60c) {
        return;
    }
    List<Positioned> list{};
    if (!read(static_cast<std::byte*>(group) + 4, list) || list.count > 4) {
        return;
    }
    auto* node = list.first;
    for (unsigned index = 0; node && index < list.count; ++index) {
        List<Positioned>::Node item{};
        Positioned entry{};
        void* text_class = nullptr;
        void* outer_class = nullptr;
        Point origin{};
        Rectangle local{};
        if (!read(node, item) || !read(item.value, entry) || !entry.drawable || entry.effect ||
            !read(entry.drawable, text_class) || text_class != image + profile->text ||
            !read(entry.drawable - 0x18, outer_class) ||
            outer_class != image + profile->credit_outer ||
            !read(entry.drawable - 0x18 + profile->credit_position, origin) ||
            !read(entry.drawable + 0x48, local)) {
            break;
        }
        if (local.bounds.left >= -640 && local.bounds.top >= -480 && local.bounds.right <= 640 &&
            local.bounds.bottom <= 480 && origin.value.x >= 0 && origin.value.x < 640 &&
            origin.value.y >= 0 && origin.value.y < 480 && !IsRectEmpty(&local.bounds)) {
            action(item.value, entry, origin, local);
        }
        node = item.next;
    }
}

RECT placed(Rectangle local, POINT position) {
    OffsetRect(&local.bounds, position.x, position.y);
    ++local.bounds.right;
    ++local.bounds.bottom;
    return local.bounds;
}

void dirty(void* group, Rectangle rectangle) {
    reinterpret_cast<Invalidate>(image + profile->credit_invalidate)(group, &rectangle);
}

int __stdcall update_group(void* group, void* context) {
    const auto result = original_update(group, context);
    credits(group,
            [&](Positioned*, const Positioned& entry, const Point& origin, Rectangle rectangle) {
                if (caption_surface::any() || entry.position.value.y != origin.value.y) {
                    rectangle.bounds = placed(rectangle, origin.value);
                    // Lower layers must repaint every possible credit position before text is
                    // drawn.
                    rectangle.bounds.top = 0;
                    dirty(group, rectangle);
                }
            });
    return result;
}

int __stdcall draw_group(void* group, void* destination, void* clip) {
    try {
        const auto dc = canvas_dc();
        credits(group, [&](Positioned* address, const Positioned& entry, const Point& origin,
                           Rectangle rectangle) {
            auto desired = origin.value;
            desired.y += caption_surface::credit_offset(dc, placed(rectangle, origin.value));
            if (desired.y != entry.position.value.y) {
                auto old = rectangle;
                old.bounds = placed(rectangle, entry.position.value);
                dirty(group, old);
                old.bounds = placed(rectangle, desired);
                dirty(group, old);
                address->position.value = desired;
                trace_value("credit_caption_offset", desired.y - origin.value.y);
            }
        });
    } catch (...) {
        caption_surface::clear();
    }
    return original_draw(group, destination, clip);
}

int __stdcall transfer_canvas(void* canvas, void* destination, Rectangle* source, Rectangle* target,
                              void* clip, int flags) {
    const CanvasPresentation presentation(canvas == current_canvas() ? canvas_dc() : nullptr);
    return original_transfer(canvas, destination, source, target, clip, flags);
}
}

HDC canvas_dc() {
    const auto canvas = current_canvas();
    std::byte* backend = nullptr;
    unsigned kind = 0;
    quickdraw::Port* port = nullptr;
    if (!canvas || !read(canvas + 0x2c, backend) || !backend || !read(backend + 4, kind) ||
        kind != 1 || !read(backend + 0xc, port)) {
        return nullptr;
    }
    return quickdraw::port_dc(port);
}

HDC presentation_source(HDC source) {
    if ((canvas_source || canvas_overlay) && presenting && source == presenting && !substituting) {
        substituting = true;
        try {
            auto replacement = canvas_source ? canvas_source(source) : source;
            if (canvas_overlay) {
                try {
                    const auto decorated = canvas_overlay(replacement ? replacement : source);
                    if (decorated) {
                        replacement = decorated;
                    }
                } catch (...) {
                }
            }
            if (replacement && replacement != source && !source_reported) {
                source_reported = true;
                trace_value("native_canvas_source", 1);
            }
            substituting = false;
            return replacement ? replacement : source;
        } catch (...) {
            substituting = false;
        }
    }
    return source;
}

void set_canvas_source(CanvasSource callback) {
    canvas_source = callback;
    source_reported = false;
}

void set_canvas_overlay(CanvasSource callback) {
    canvas_overlay = callback;
    source_reported = false;
}

CanvasPresentation::CanvasPresentation(HDC source) : previous_(presenting) {
    presenting = source;
}

CanvasPresentation::~CanvasPresentation() {
    presenting = previous_;
}

bool native_render_available() {
    return attached && canvas_dc() != nullptr;
}

void invalidate_canvas() {
    const auto canvas = current_canvas();
    void** vtable = nullptr;
    void* method = nullptr;
    if (!attached || !canvas || !read(canvas, vtable) ||
        !read(vtable + 0x4c / sizeof(void*), method) ||
        method != image + profile->canvas_invalidate) {
        return;
    }
    Rectangle bounds{nullptr, {0, 0, 640, 480}};
    reinterpret_cast<Invalidate>(method)(canvas, &bounds);
}

void attach_native_render() {
    if (attached || !(image = enhancements::game::executable_image())) {
        return;
    }
    profile = &enhancements::game::edition();
    auto** table = reinterpret_cast<void**>(image + profile->credit_group);
    original_update = reinterpret_cast<Update>(image + profile->credit_update);
    original_draw = reinterpret_cast<Draw>(image + profile->credit_draw);
    original_transfer = reinterpret_cast<Transfer>(image + profile->canvas_transfer);
    auto* call = image + profile->canvas_transfer_call;
    std::int32_t displacement = 0;
    std::memcpy(&displacement, call + 1, sizeof(displacement));
    if (table[0x30 / 4] != reinterpret_cast<void*>(original_update) ||
        table[0x34 / 4] != reinterpret_cast<void*>(original_draw) || call[0] != std::byte{0xe8} ||
        call + 5 + displacement != image + profile->canvas_transfer || !attach_render_imports()) {
        return;
    }
    std::memcpy(original_call.data(), call, original_call.size());
    auto patched = original_call;
    const auto relative =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&transfer_canvas) -
                                   reinterpret_cast<std::uintptr_t>(call + 5));
    std::memcpy(patched.data() + 1, &relative, sizeof(relative));
    const auto update = &update_group;
    const auto draw = &draw_group;
    if (!replace(table + 0x30 / 4, &update, sizeof(update)) ||
        !replace(table + 0x34 / 4, &draw, sizeof(draw)) ||
        !replace(call, patched.data(), patched.size())) {
        replace(table + 0x30 / 4, &original_update, sizeof(original_update));
        replace(table + 0x34 / 4, &original_draw, sizeof(original_draw));
        detach_render_imports();
        return;
    }
    attached = true;
    trace_value("native_render_attached", 1);
}

void detach_native_render() {
    if (attached) {
        auto** table = reinterpret_cast<void**>(image + profile->credit_group);
        replace(table + 0x30 / 4, &original_update, sizeof(original_update));
        replace(table + 0x34 / 4, &original_draw, sizeof(original_draw));
        replace(image + profile->canvas_transfer_call, original_call.data(), original_call.size());
    }
    detach_render_imports();
    caption_surface::clear();
    canvas_source = nullptr;
    canvas_overlay = nullptr;
    presenting = nullptr;
    attached = false;
    image = nullptr;
    profile = nullptr;
}
}
