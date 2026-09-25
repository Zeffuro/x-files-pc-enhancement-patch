#include "world.h"
#include "types.h"
#include "regions.h"
#include "palette.h"
#include "graphics.h"
#include "enhancements/controls.h"

#include <windows.h>
#include <memory>
#include <unordered_map>

namespace {

using namespace quickdraw;

constexpr std::uint32_t pixels_locked = 1 << 7;

struct World {
    Port port{};
    Color operation{32768, 32768, 32768};
    PixMap pixels{};
    PixMap* pixel_handle = &pixels;
    Device device{};
    Device* device_handle = &device;
    ColorTable palette{};
    ColorTable* palette_handle = &palette;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous_bitmap = nullptr;
    bool locked = false;
    HWND window = nullptr;
    bool associated = false;

    ~World() {
        if (previous_bitmap) {
            SelectObject(dc, previous_bitmap);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (dc) {
            DeleteDC(dc);
        }
    }
};

thread_local std::unordered_map<Port*, std::unique_ptr<World>> worlds;
thread_local Port* current_port = nullptr;

Port make_default_port() {
    Port port{};
    port.version = 0xc000;
    port.background = {65535, 65535, 65535};
    port.pen_size = {1, 1};
    port.foreground_color = 33;
    port.background_color = 30;
    return port;
}

thread_local Port default_port = make_default_port();
thread_local Color default_operation{32768, 32768, 32768};

World* find(Port* port) {
    const auto entry = worlds.find(port);
    return entry == worlds.end() ? nullptr : entry->second.get();
}

World* find(PixMapHandle pixels) {
    for (const auto& [port, world] : worlds) {
        if (pixels == &world->pixel_handle) {
            return world.get();
        }
    }
    return nullptr;
}

short __cdecl new_world(Port** output, std::uint32_t format, const Rect* bounds, void* color_table,
                        void* device, std::uint32_t flags) {
    trace_value("world_format", format);
    trace_value("world_flags", flags);
    if (!output) {
        return -50;
    }
    *output = nullptr;
    if (!bounds ||
        (format != bgra_format && format != bgr_format && format != monochrome_format &&
         format != indexed_format) ||
        color_table || device || flags) {
        return -50;
    }

    const int width = bounds->right - bounds->left;
    const int height = bounds->bottom - bounds->top;
    if (width <= 0 || height <= 0 || width > 4095 || height > 16384) {
        return -50;
    }

    RegionHandle visible = nullptr;
    RegionHandle clip = nullptr;
    try {
        auto world = std::make_unique<World>();
        world->dc = CreateCompatibleDC(nullptr);
        if (!world->dc) {
            return -108;
        }

        const bool monochrome = format == monochrome_format;
        const bool indexed = format == indexed_format;
        const WORD depth = monochrome ? 1 : indexed ? 8 : format == bgr_format ? 24 : 32;
        const int stride = ((width * depth + 31) / 32) * 4;

        struct {
            BITMAPINFOHEADER header;
            RGBQUAD colors[256];
        } description{};

        description.header.biSize = sizeof(BITMAPINFOHEADER);
        description.header.biWidth = width;
        description.header.biHeight = -height;
        description.header.biPlanes = 1;
        description.header.biBitCount = depth;
        description.header.biCompression = BI_RGB;
        if (monochrome) {
            description.header.biClrUsed = 2;
            description.colors[0] = {255, 255, 255, 0};
        } else if (indexed) {
            description.header.biClrUsed = 256;
            world->palette = default_palette();
            for (std::size_t index = 0; index < 256; ++index) {
                const auto& color = world->palette.colors[index].color;
                description.colors[index] = {static_cast<BYTE>(color.blue >> 8),
                                             static_cast<BYTE>(color.green >> 8),
                                             static_cast<BYTE>(color.red >> 8), 0};
            }
        }
        void* buffer = nullptr;
        world->bitmap = CreateDIBSection(world->dc, reinterpret_cast<BITMAPINFO*>(&description),
                                         DIB_RGB_COLORS, &buffer, nullptr, 0);
        if (!world->bitmap || !buffer) {
            return -108;
        }
        world->previous_bitmap = SelectObject(world->dc, world->bitmap);
        if (!world->previous_bitmap || world->previous_bitmap == HGDI_ERROR) {
            world->previous_bitmap = nullptr;
            return -108;
        }
        if (!SetViewportOrgEx(world->dc, -bounds->left, -bounds->top, nullptr)) {
            return -108;
        }

        auto& pixels = world->pixels;
        pixels.base = static_cast<std::uint8_t*>(buffer);
        pixels.row_bytes = static_cast<std::uint16_t>(pixmap_flag | stride);
        pixels.bounds = *bounds;
        pixels.version = 4;
        pixels.horizontal_resolution = 72 << 16;
        pixels.vertical_resolution = 72 << 16;
        pixels.pixel_type = depth <= 8 ? 0 : direct_pixel_type;
        pixels.pixel_size = depth;
        pixels.component_count = depth <= 8 ? 1 : 3;
        pixels.component_size = depth <= 8 ? depth : 8;
        pixels.pixel_format = format;
        pixels.color_table = indexed ? &world->palette_handle : nullptr;
        world->device.type = monochrome ? DeviceType::Fixed
                             : indexed  ? DeviceType::Indexed
                                        : DeviceType::Direct;
        world->device.pixels = &world->pixel_handle;
        world->device.bounds = *bounds;

        auto& port = world->port;
        port.pixels = &world->pixel_handle;
        port.version = 0xc000;
        port.bounds = *bounds;
        visible = create_region(*bounds);
        clip = create_region(*bounds);
        port.visible_region = visible;
        port.clip_region = clip;
        port.background = {65535, 65535, 65535};
        port.pen_size = {1, 1};
        port.foreground_color = 33;
        port.background_color = 30;

        Port* result = &world->port;
        worlds.emplace(result, std::move(world));
        *output = result;
        trace_value("world_created", reinterpret_cast<std::uint32_t>(result));
        return 0;
    } catch (const std::bad_alloc&) {
        dispose_region(visible);
        dispose_region(clip);
        return -108;
    }
}

void __cdecl dispose_world(Port* port) {
    trace_value("world_disposed", reinterpret_cast<std::uint32_t>(port));
    if (current_port == port) {
        current_port = nullptr;
    }
    if (const auto world = find(port)) {
        dispose_region(world->port.visible_region);
        dispose_region(world->port.clip_region);
        worlds.erase(port);
    }
}

Port* __cdecl associate_window(HWND window, void* storage, std::uint32_t flags) {
    constexpr auto no_idle_events = 1u << 1;
    constexpr auto no_double_buffer = 1u << 2;
    if (!IsWindow(window) || storage || (flags & ~(no_idle_events | no_double_buffer))) {
        return nullptr;
    }
    for (const auto& [port, world] : worlds) {
        if (world->window == window && world->associated) {
            return port;
        }
    }
    RECT client{};
    if (!GetClientRect(window, &client) || client.right > INT16_MAX || client.bottom > INT16_MAX) {
        return nullptr;
    }
    const Rect bounds{0, 0, static_cast<std::int16_t>(client.bottom),
                      static_cast<std::int16_t>(client.right)};
    Port* port = nullptr;
    if (new_world(&port, bgra_format, &bounds, nullptr, nullptr, 0) != 0) {
        return nullptr;
    }
    find(port)->window = window;
    find(port)->associated = true;
    enhancements::attach_controls(window);
    return port;
}

void __cdecl detach_window(Port* port) {
    const auto world = find(port);
    if (!world || !world->window) {
        return;
    }
    // The game retains borrowed window ports after removing their association.
    world->associated = false;
    if (!IsWindow(world->window)) {
        dispose_world(port);
    }
}

Port* __cdecl window_port(HWND window) {
    for (const auto& [port, world] : worlds) {
        if (world->window == window && world->associated) {
            return port;
        }
    }
    return nullptr;
}

PixMapHandle __cdecl get_pixels(Port* port) {
    const auto world = find(port);
    return world ? &world->pixel_handle : nullptr;
}

DeviceHandle __cdecl get_device(Port* port) {
    const auto world = find(port);
    return world ? &world->device_handle : nullptr;
}

unsigned char __cdecl lock_pixels(PixMapHandle pixels) {
    const auto world = find(pixels);
    if (!world) {
        return 0;
    }
    GdiFlush();
    world->locked = true;
    return 1;
}

void __cdecl unlock_pixels(PixMapHandle pixels) {
    if (const auto world = find(pixels)) {
        world->locked = false;
    }
}

std::uint32_t __cdecl pixels_state(PixMapHandle pixels) {
    const auto world = find(pixels);
    return world && world->locked ? pixels_locked : 0;
}

HDC __cdecl get_dc(Port* port) {
    const auto world = find(port);
    return world ? world->dc : nullptr;
}

std::uint8_t* __cdecl pixel_address(PixMapHandle pixels) {
    const auto world = find(pixels);
    return world ? world->pixels.base : nullptr;
}

void __cdecl get_world(Port** port, void** device) {
    if (port) {
        *port = current_port;
    }
    if (device) {
        *device = nullptr;
    }
}

void __cdecl set_world(Port* port, void* device) {
    if (device || (port && !find(port))) {
        trace_value("set_world_port", reinterpret_cast<std::uint32_t>(port));
        trace_value("set_world_device", reinterpret_cast<std::uint32_t>(device));
        unsupported(Selector::SetGWorld, "SetGWorld: unknown port or device", 0);
    }
    current_port = port;
}

void __cdecl local_to_global(Point* point) {
    const auto world = find(current_port);
    if (!point || !world) {
        return;
    }
    POINT native{point->x, point->y};
    if (!LPtoDP(world->dc, &native, 1) ||
        (world->window && !ClientToScreen(world->window, &native))) {
        unsupported(Selector::LocalToGlobal, "Cannot convert drawing coordinates", 0);
    }
    point->x = static_cast<std::int16_t>(native.x);
    point->y = static_cast<std::int16_t>(native.y);
}

}

namespace quickdraw {

Port& drawing_port() {
    return current_port ? *current_port : default_port;
}

Color& operation_color() {
    const auto world = find(current_port);
    return world ? world->operation : default_operation;
}

HDC port_dc(Port* port) {
    return get_dc(port);
}

HDC pixel_dc(const PixMap* pixels) {
    if (!pixels) {
        return nullptr;
    }
    for (const auto& [port, world] : worlds) {
        if (world->pixels.base && world->pixels.base == pixels->base &&
            world->pixels.row_bytes == pixels->row_bytes) {
            return world->dc;
        }
    }
    return nullptr;
}

void present_port(Port* port, const Rect& bounds) {
    const auto world = find(port);
    if (world && world->window) {
        present_graphics(world->window, world->dc, bounds);
    }
}

void present_pixels(const PixMap* pixels, const Rect& bounds) {
    if (!pixels) {
        return;
    }
    for (const auto& [port, world] : worlds) {
        if (world->window && world->pixels.base == pixels->base) {
            present_port(port, bounds);
        }
    }
}

}

Entry world_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::QTNewGWorld, new_world),
        bind_entry(Selector::GetPortHDC, get_dc),
        bind_entry(Selector::LockPixels, lock_pixels),
        bind_entry(Selector::UnlockPixels, unlock_pixels),
        bind_entry(Selector::DisposeGWorld, dispose_world),
        bind_entry(Selector::GetGWorld, get_world),
        bind_entry(Selector::SetGWorld, set_world),
        bind_entry(Selector::LocalToGlobal, local_to_global),
        bind_entry(Selector::GetPixelsState, pixels_state),
        bind_entry(Selector::GetPixBaseAddr, pixel_address),
        bind_entry(Selector::GetGWorldPixMap, get_pixels),
        bind_entry(Selector::GetGWorldDevice, get_device),
        bind_entry(Selector::CreatePortAssociation, associate_window),
        bind_entry(Selector::DestroyPortAssociation, detach_window),
        bind_entry(Selector::GetNativeWindowPort, window_port),
    };
    if (const auto entry = find_entry(selector, entries)) {
        return entry;
    }
    if (const auto entry = blit_entry(selector)) {
        return entry;
    }
    return drawing_entry(selector);
}

void release_worlds() {
    enhancements::detach_controls();
    current_port = nullptr;
    for (const auto& [port, world] : worlds) {
        quickdraw::dispose_region(port->visible_region);
        quickdraw::dispose_region(port->clip_region);
    }
    worlds.clear();
    default_port = make_default_port();
    default_operation = {32768, 32768, 32768};
}
