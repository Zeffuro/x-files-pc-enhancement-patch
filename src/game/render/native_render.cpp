#include "native_render.h"
#include "render_internal.h"
#include "caption_surface.h"
#include "enhancements/game_ui.h"
#include "game/access/render.h"
#include "game/functions/runtime.h"
#include "quickdraw/world.h"
#include "runtime.h"
#include <array>
#include <cstring>

namespace native_game {
namespace {
using Update = CreditGroupVTable::Update;
using Draw = CreditGroupVTable::Draw;
using Invalidate = CanvasVTable::Invalidate;
using Transfer = int(__stdcall*)(void*, void*, Rectangle*, Rectangle*, void*, int);
Update original_update = nullptr;
Draw original_draw = nullptr;
Transfer original_transfer = nullptr;
std::byte* image = nullptr;
const Profile* profile = nullptr;
constexpr std::byte relative_call_opcode{0xe8};
constexpr std::size_t relative_call_operand = 1;
constexpr std::size_t relative_call_size = relative_call_operand + sizeof(std::int32_t);
std::array<std::byte, relative_call_size> original_call{};
bool attached = false;
thread_local CanvasSource canvas_source = nullptr;
thread_local CanvasSource canvas_overlay = nullptr;
thread_local CanvasSource canvas_targets = nullptr;
thread_local CanvasSource canvas_reveal = nullptr;
thread_local CanvasSource canvas_reader = nullptr;
thread_local CanvasSource canvas_status = nullptr;
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
    return profile && view ? MainViewRef(view, profile).canvas().data() : nullptr;
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

template <typename Action> void credits(void* group, Action action) {
    const auto view = enhancements::game::current_view();
    if (!view || group != MainViewCreditsRef(view).credits().data()) {
        return;
    }
    List<PositionedDrawable> list{};
    if (!read(CreditGroupRef(group).drawables().data(), list) || list.count > 4) {
        return;
    }
    auto* node = list.first;
    for (unsigned index = 0; node && index < list.count; ++index) {
        List<PositionedDrawable>::Node item{};
        PositionedDrawable entry{};
        void* text_class = nullptr;
        void* outer_class = nullptr;
        RenderPoint origin{};
        Rectangle local{};
        if (!read(node, item) || !read(item.value, entry) || !entry.drawable || entry.effect ||
            !read(entry.drawable, text_class) ||
            text_class != NativeRuntime(image, *profile).text_type() ||
            !read(CreditTextRef(entry.drawable).owner().data(), outer_class) ||
            outer_class != NativeRuntime(image, *profile).credit_outer_type() ||
            !read(CreditTextRef(entry.drawable, profile).owner().position().data(), origin) ||
            !read(CreditTextRef(entry.drawable).rectangle().data(), local)) {
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
    NativeFunctions(image, *profile).credit_invalidate(group, &rectangle);
}

int __stdcall update_group(void* group, void* context) {
    const auto result = original_update(group, context);
    credits(group, [&](PositionedDrawable*, const PositionedDrawable& entry,
                       const RenderPoint& origin, Rectangle rectangle) {
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
        credits(group, [&](PositionedDrawable* address, const PositionedDrawable& entry,
                           const RenderPoint& origin, Rectangle rectangle) {
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
    CanvasBackendKind kind{};
    quickdraw::Port* port = nullptr;
    if (!canvas || !read(CanvasRef(canvas).backend().data(), backend) || !backend ||
        !read(CanvasBackendRef(backend).kind().data(), kind) ||
        kind != CanvasBackendKind::quickdraw ||
        !read(CanvasBackendRef(backend).port().data(), port)) {
        return nullptr;
    }
    return quickdraw::port_dc(port);
}

HDC presentation_source(HDC source) {
    if ((canvas_source || canvas_overlay || canvas_targets || canvas_reveal || canvas_reader ||
         canvas_status) &&
        presenting && source == presenting && !substituting) {
        substituting = true;
        try {
            auto replacement = source;
            for (const auto decorate : {canvas_source, canvas_reveal, canvas_overlay,
                                        canvas_targets, canvas_reader, canvas_status}) {
                if (!decorate) {
                    continue;
                }
                try {
                    const auto decorated = decorate(replacement);
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

void set_canvas_targets(CanvasSource callback) {
    canvas_targets = callback;
    source_reported = false;
}

void set_canvas_reveal(CanvasSource callback) {
    canvas_reveal = callback;
    source_reported = false;
}

void set_canvas_reader(CanvasSource callback) {
    canvas_reader = callback;
    source_reported = false;
}

void set_canvas_status(CanvasSource callback) {
    canvas_status = callback;
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
    CanvasVTable* vtable = nullptr;
    Invalidate method = nullptr;
    if (!attached || !canvas || !read(canvas, vtable) ||
        !read(CanvasVTableRef(vtable).invalidate().get(), method) ||
        method != NativeFunctions(image, *profile).canvas_invalidate_target()) {
        return;
    }
    Rectangle bounds{nullptr, {0, 0, 640, 480}};
    method(canvas, &bounds);
}

void attach_native_render() {
    if (attached || !(image = enhancements::game::executable_image())) {
        return;
    }
    profile = &enhancements::game::edition();
    auto* table =
        MemoryRef<CreditGroupVTable>(NativeRuntime(image, *profile).credit_group_type()).get();
    original_update = NativeFunctions(image, *profile).credit_update_target();
    original_draw = NativeFunctions(image, *profile).credit_draw_target();
    original_transfer = NativeFunctions(image, *profile).canvas_transfer_target();
    auto* call = image + profile->canvas_transfer_call;
    std::int32_t displacement = 0;
    std::memcpy(&displacement, call + relative_call_operand, sizeof(displacement));
    if (table->update != original_update || table->draw != original_draw ||
        call[0] != relative_call_opcode ||
        call + relative_call_size + displacement != image + profile->canvas_transfer ||
        !attach_render_imports()) {
        return;
    }
    std::memcpy(original_call.data(), call, original_call.size());
    auto patched = original_call;
    const auto relative =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&transfer_canvas) -
                                   reinterpret_cast<std::uintptr_t>(call + relative_call_size));
    std::memcpy(patched.data() + relative_call_operand, &relative, sizeof(relative));
    const auto update = &update_group;
    const auto draw = &draw_group;
    if (!replace(&table->update, &update, sizeof(update)) ||
        !replace(&table->draw, &draw, sizeof(draw)) ||
        !replace(call, patched.data(), patched.size())) {
        replace(&table->update, &original_update, sizeof(original_update));
        replace(&table->draw, &original_draw, sizeof(original_draw));
        detach_render_imports();
        return;
    }
    attached = true;
    trace_value("native_render_attached", 1);
}

void detach_native_render() {
    if (attached) {
        auto* table =
            MemoryRef<CreditGroupVTable>(NativeRuntime(image, *profile).credit_group_type()).get();
        replace(&table->update, &original_update, sizeof(original_update));
        replace(&table->draw, &original_draw, sizeof(original_draw));
        replace(image + profile->canvas_transfer_call, original_call.data(), original_call.size());
    }
    detach_render_imports();
    caption_surface::clear();
    canvas_source = nullptr;
    canvas_overlay = nullptr;
    canvas_targets = nullptr;
    canvas_reveal = nullptr;
    canvas_reader = nullptr;
    canvas_status = nullptr;
    presenting = nullptr;
    attached = false;
    image = nullptr;
    profile = nullptr;
}
}
