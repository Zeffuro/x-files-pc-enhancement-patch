#include "quickdraw/types.h"
#include "dispatch.h"

#include <windows.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using namespace quickdraw;

using namespace test;

Port* create(const Rect& bounds);

RectangleProcedure original_rectangle = nullptr;
unsigned rectangle_calls = 0;

void __cdecl custom_rectangle(Verb verb, const Rect* rectangle) {
    ++rectangle_calls;
    original_rectangle(verb, rectangle);
}

void verify_callbacks() {
    struct {
        std::uint32_t before = 0x12345678;
        Procedures procedures{};
        std::uint32_t after = 0x87654321;
    } table;

    invoke(Selector::SetStdCProcs, address(&table.procedures));
    require(table.before == 0x12345678 && table.after == 0x87654321,
            "SetStdCProcs wrote outside its table.");
    require(table.procedures.bits && table.procedures.pixels && table.procedures.rectangle,
            "Standard drawing callbacks are missing.");
    const auto port = create({0, 0, 2, 2});
    invoke(Selector::SetGWorld, address(port));
    original_rectangle = table.procedures.rectangle;
    table.procedures.rectangle = custom_rectangle;
    port->procedures = &table.procedures;
    invoke(Selector::ForeColor, 205);
    invoke(Selector::PaintRect, address(&port->bounds));
    require(rectangle_calls == 1 && (*port->pixels)->base[2] == 255,
            "PaintRect did not invoke the port callback and its saved standard procedure.");
    invoke(Selector::EraseRect, address(&port->bounds));
    require(rectangle_calls == 2 && (*port->pixels)->base[0] == 255,
            "EraseRect did not invoke the port callback.");
    invoke(Selector::DisposeGWorld, address(port));
}

Port* create(const Rect& bounds) {
    Port* port = nullptr;
    const auto error = static_cast<short>(
        invoke(Selector::QTNewGWorld, address(&port), 0x42475241, address(&bounds)));
    require(error == 0 && port, "QTNewGWorld failed.");
    return port;
}

void verify_pixels() {
    const Rect bounds{10, 20, 13, 25};
    const auto port = create(bounds);
    const auto pixels =
        reinterpret_cast<PixMapHandle>(invoke(Selector::GetGWorldPixMap, address(port)));
    require(pixels && pixels == port->pixels, "PixMap handle differs from the port record.");
    const auto& map = **pixels;
    require((map.row_bytes & 0x3fff) == 20 && map.pixel_size == 32 &&
                map.pixel_format == 0x42475241 && map.bounds.left == 20,
            "PixMap fields have the wrong layout or values.");

    require((invoke(Selector::LockPixels, address(pixels)) & 0xff) == 1, "LockPixels failed.");
    require(invoke(Selector::GetPixelsState, address(pixels)) == 128, "Locked state missing.");
    const auto dc = reinterpret_cast<HDC>(invoke(Selector::GetPortHDC, address(port)));
    require(dc && SetPixel(dc, 20, 10, RGB(18, 52, 86)) == RGB(18, 52, 86), "GDI drawing failed.");
    GdiFlush();
    require(map.base[0] == 86 && map.base[1] == 52 && map.base[2] == 18,
            "GDI and the PixMap do not share BGRA storage.");

    const auto last_row = map.base + 2 * (map.row_bytes & 0x3fff);
    last_row[0] = 24;
    last_row[1] = 48;
    last_row[2] = 96;
    require(GetPixel(dc, 20, 12) == RGB(96, 48, 24), "Rows are not stored top to bottom.");

    invoke(Selector::UnlockPixels, address(pixels));
    require(invoke(Selector::GetPixelsState, address(pixels)) == 0,
            "UnlockPixels did not clear the state.");
    invoke(Selector::SetGWorld, address(port));
    Port* selected = nullptr;
    void* device = reinterpret_cast<void*>(1);
    invoke(Selector::GetGWorld, address(&selected), address(&device));
    require(selected == port && !device, "Current port was not preserved.");

    invoke(Selector::DisposeGWorld, address(port));
    require(invoke(Selector::GetGWorldPixMap, address(port)) == 0,
            "Disposed world is still registered.");
    invoke(Selector::GetGWorld, address(&selected));
    require(!selected, "Disposal left a dangling current port.");
}

void verify_errors() {
    Rect bounds{0, 0, 480, 640};
    Port* result = reinterpret_cast<Port*>(1);
    require(static_cast<short>(
                invoke(Selector::QTNewGWorld, address(&result), 0, address(&bounds))) == -50 &&
                !result,
            "Unsupported pixel format was accepted.");
    bounds.right = bounds.left;
    require(static_cast<short>(invoke(Selector::QTNewGWorld, address(&result), 0x42475241,
                                      address(&bounds))) == -50,
            "Empty bounds were accepted.");
    require((invoke(Selector::LockPixels, 1) & 0xff) == 0, "An invalid pixel handle was accepted.");
}

void verify_monochrome() {
    const Rect bounds{3, 7, 5, 16};
    Port* port = nullptr;
    const auto error =
        static_cast<short>(invoke(Selector::QTNewGWorld, address(&port), 1, address(&bounds)));
    require(error == 0 && port, "Monochrome QTNewGWorld failed.");
    const auto& pixels = **port->pixels;
    require(pixels.pixel_size == 1 && pixels.component_count == 1 &&
                (pixels.row_bytes & 0x3fff) == 4,
            "Monochrome pixel depth or row alignment is wrong.");
    const auto dc = reinterpret_cast<HDC>(invoke(Selector::GetPortHDC, address(port)));
    invoke(Selector::SetGWorld, address(port));
    invoke(Selector::EraseRect, address(&bounds));
    require(GetPixel(dc, 7, 3) == RGB(255, 255, 255) && pixels.base[0] == 0,
            "Monochrome zero bits should be white.");
    SetPixel(dc, 7, 3, RGB(0, 0, 0));
    SetPixel(dc, 15, 4, RGB(0, 0, 0));
    GdiFlush();
    require(pixels.base[0] == 0x80 && pixels.base[4] == 0 && pixels.base[5] == 0x80,
            "Monochrome pixels have the wrong bit order or row stride.");
    invoke(Selector::DisposeGWorld, address(port));
}

void verify_indexed() {
    const Rect bounds{2, 3, 4, 8};
    Port* port = nullptr;
    require(static_cast<short>(invoke(Selector::QTNewGWorld, address(&port), indexed_format,
                                      address(&bounds))) == 0 &&
                port,
            "Indexed QTNewGWorld failed.");
    const auto& pixels = **port->pixels;
    require(pixels.pixel_size == 8 && pixels.component_count == 1 && pixels.component_size == 8 &&
                (pixels.row_bytes & row_bytes_mask) == 8 && pixels.color_table,
            "Indexed pixel layout or palette is missing.");
    const auto& palette = **static_cast<ColorTableHandle>(pixels.color_table);
    require(palette.last_entry == 255 && palette.colors[0].color.red == 65535 &&
                palette.colors[35].color.green == 0 && palette.colors[255].color.red == 0,
            "Indexed palette endpoints are incorrect.");
    const auto dc = reinterpret_cast<HDC>(invoke(Selector::GetPortHDC, address(port)));
    pixels.base[0] = 0;
    pixels.base[1] = 35;
    pixels.base[2] = 255;
    require(GetPixel(dc, 3, 2) == RGB(255, 255, 255) && GetPixel(dc, 4, 2) == RGB(255, 0, 0) &&
                GetPixel(dc, 5, 2) == RGB(0, 0, 0),
            "Palette indices did not produce the expected colors.");
    const auto destination = create(bounds);
    invoke(Selector::CopyBits, address(*port->pixels), address(*destination->pixels),
           address(&bounds), address(&bounds));
    const auto output = (*destination->pixels)->base;
    require(output[4] == 0 && output[5] == 0 && output[6] == 255,
            "Indexed-to-direct CopyBits lost the palette.");
    invoke(Selector::DisposeGWorld, address(destination));
    invoke(Selector::DisposeGWorld, address(port));
}

void verify_drawing() {
    const Rect bounds{10, 20, 14, 26};
    const auto port = create(bounds);
    const auto dc = reinterpret_cast<HDC>(invoke(Selector::GetPortHDC, address(port)));
    invoke(Selector::SetGWorld, address(port));
    invoke(Selector::BackColor, 30);
    invoke(Selector::EraseRect, address(&bounds));
    require(GetPixel(dc, 20, 10) == RGB(255, 255, 255),
            "EraseRect did not use the background color.");

    const Rect clip{11, 21, 13, 25};
    invoke(Selector::ClipRect, address(&clip));
    invoke(Selector::ForeColor, 205);
    invoke(Selector::PaintRect, address(&bounds));
    require(GetPixel(dc, 21, 11) == RGB(255, 0, 0), "PaintRect did not use the foreground color.");
    const auto outside_clip = (*port->pixels)->base;
    require(outside_clip[0] == 255 && outside_clip[1] == 255 && outside_clip[2] == 255,
            "PaintRect escaped the clipping rectangle.");

    const Color color{0x1234, 0x5678, 0x9abc};
    invoke(Selector::RGBForeColor, address(&color));
    invoke(Selector::PaintRect, address(&bounds));
    require(GetPixel(dc, 21, 11) == RGB(0x12, 0x56, 0x9a), "RGBForeColor lost color precision.");
    const auto other = create({0, 0, 2, 2});
    invoke(Selector::SetGWorld, address(other));
    invoke(Selector::ForeColor, 409);
    require(port->foreground.red == color.red && other->foreground.blue == 65535,
            "Color state leaked between drawing ports.");
    invoke(Selector::DisposeGWorld, address(other));
    invoke(Selector::DisposeGWorld, address(port));
}

void verify_bgr_copy() {
    const Rect bounds{3, 7, 5, 10};
    Port* source = nullptr;
    require(static_cast<short>(
                invoke(Selector::QTNewGWorld, address(&source), bgr_format, address(&bounds))) == 0,
            "24-bit world creation failed.");
    const auto& pixels = **source->pixels;
    require(pixels.pixel_size == 24 && (pixels.row_bytes & row_bytes_mask) == 12,
            "24-bit row padding is incorrect.");
    const auto device =
        reinterpret_cast<DeviceHandle>(invoke(Selector::GetGWorldDevice, address(source)));
    require(device && (*device)->type == DeviceType::Direct && (*device)->pixels == source->pixels,
            "World device metadata is inconsistent.");
    const auto dc = reinterpret_cast<HDC>(invoke(Selector::GetPortHDC, address(source)));
    SetPixel(dc, 7, 3, RGB(10, 20, 30));
    SetPixel(dc, 9, 4, RGB(40, 50, 60));
    GdiFlush();
    require(pixels.base[0] == 30 && pixels.base[2] == 10 && pixels.base[18] == 60,
            "24-bit row order or component layout is incorrect.");
    const auto destination = create({0, 0, 4, 6});
    invoke(Selector::SetGWorld, address(destination));
    invoke(Selector::CopyBits, address(*source->pixels), address(*destination->pixels),
           address(&bounds), address(&destination->bounds));
    const auto output = (*destination->pixels)->base;
    require(output[0] == 30 && output[2] == 10 && output[92] == 60 && output[94] == 40,
            "Scaled 24-bit to 32-bit CopyBits failed.");
    invoke(Selector::DisposeGWorld, address(destination));
    invoke(Selector::DisposeGWorld, address(source));
}

void verify_window_port() {
    const auto window = CreateWindowExW(0, L"STATIC", L"QuickDraw test", WS_POPUP, 100, 200, 80, 60,
                                        nullptr, nullptr, nullptr, nullptr);
    require(window != nullptr, "Test window creation failed.");
    const auto port =
        reinterpret_cast<Port*>(invoke(Selector::CreatePortAssociation, address(window)));
    require(port && port->bounds.right == 80 && port->bounds.bottom == 60,
            "Window port dimensions are incorrect.");
    require(invoke(Selector::GetNativeWindowPort, address(window)) == address(port),
            "Window association lookup failed.");
    invoke(Selector::SetGWorld, address(port));
    Point point{7, 11};
    POINT expected{11, 7};
    require(ClientToScreen(window, &expected), "Cannot locate the test window.");
    invoke(Selector::LocalToGlobal, address(&point));
    require(point.x == expected.x && point.y == expected.y,
            "LocalToGlobal ignored the associated window position.");
    invoke(Selector::PaintRect, address(&port->bounds));
    invoke(Selector::DestroyPortAssociation, address(port));
    require(invoke(Selector::GetNativeWindowPort, address(window)) == 0,
            "Disposed window association remains registered.");
    invoke(Selector::SetGWorld, address(port));
    invoke(Selector::PaintRect, address(&port->bounds));
    DestroyWindow(window);
    invoke(Selector::DestroyPortAssociation, address(port));
}

void verify_blended_copy() {
    const auto source = create({3, 7, 4, 8});
    const auto destination = create({10, 20, 12, 24});
    const auto source_dc = reinterpret_cast<HDC>(invoke(Selector::GetPortHDC, address(source)));
    const auto destination_dc =
        reinterpret_cast<HDC>(invoke(Selector::GetPortHDC, address(destination)));
    SetPixel(source_dc, 7, 3, RGB(200, 100, 40));
    invoke(Selector::SetGWorld, address(destination));
    const Color background{20 * 257, 60 * 257, 180 * 257};
    const Color weight{65535, 32768, 0};
    invoke(Selector::RGBBackColor, address(&background));
    invoke(Selector::EraseRect, address(&destination->bounds));
    invoke(Selector::OpColor, address(&weight));
    invoke(Selector::SetGWorld, address(source));
    const Color other_weight{};
    invoke(Selector::OpColor, address(&other_weight));
    invoke(Selector::SetGWorld, address(destination));
    const Rect clip{10, 21, 12, 23};
    invoke(Selector::ClipRect, address(&clip));
    invoke(Selector::CopyBits, address(*source->pixels), address(*destination->pixels),
           address(&source->bounds), address(&destination->bounds),
           static_cast<unsigned>(TransferMode::Blend));
    invoke(Selector::ClipRect, address(&destination->bounds));
    require(GetPixel(destination_dc, 21, 11) == RGB(200, 80, 180),
            "Blend lost channel weights, scaling, or per-port operation color.");
    require(GetPixel(destination_dc, 20, 11) == RGB(20, 60, 180) &&
                GetPixel(destination_dc, 23, 11) == RGB(20, 60, 180),
            "Blending ignored destination clipping.");
    invoke(Selector::DisposeGWorld, address(source));
    invoke(Selector::DisposeGWorld, address(destination));
}

void verify_matte() {
    const auto source = create({3, 7, 4, 9});
    const auto destination = create({10, 20, 12, 24});
    const auto borrowed = create({0, 0, 2, 2});
    const auto original_pixels = borrowed->pixels;
    const auto original_bounds = borrowed->bounds;
    borrowed->pixels = destination->pixels;
    borrowed->bounds = destination->bounds;
    invoke(Selector::SetGWorld, address(source));
    invoke(Selector::ForeColor, 205);
    invoke(Selector::PaintRect, address(&source->bounds));
    invoke(Selector::SetGWorld, address(destination));
    invoke(Selector::ForeColor, 409);
    invoke(Selector::PaintRect, address(&destination->bounds));
    invoke(Selector::SetGWorld, address(borrowed));
    const Rect clip{10, 20, 12, 24};
    invoke(Selector::MacSetRectRgn, address(borrowed->clip_region), 20, 10, 24, 12);
    Procedures procedures{};
    invoke(Selector::SetStdCProcs, address(&procedures));
    Matrix matrix{};
    invoke(Selector::RectMatrix, address(&matrix), address(&source->bounds), address(&clip));
    require(matrix.values[0] == 2 * 65536 && matrix.values[4] == 2 * 65536 &&
                matrix.values[6] == 6 * 65536 && matrix.values[7] == 4 * 65536,
            "RectMatrix did not map translated source and destination rectangles.");
    std::uint8_t mask_data[]{0x80, 0, 0, 0};
    PixMap mask{};
    mask.base = mask_data;
    mask.row_bytes = 4;
    mask.bounds = source->bounds;
    mask.pixel_size = 1;
    procedures.pixels(*source->pixels, &source->bounds, &matrix, 0, nullptr, &mask, &mask.bounds,
                      1);
    const auto pixels = (*destination->pixels)->base;
    require(pixels[2] == 255 && pixels[0] == 0 && pixels[8] == 255 && pixels[10] == 0,
            "Monochrome matte or borrowed destination PixMap was not respected.");
    std::uint8_t alpha[]{128, 0, 0, 0};
    mask.base = alpha;
    mask.pixel_size = 8;
    mask.bounds.right = mask.bounds.left + 1;
    procedures.pixels(*source->pixels, &source->bounds, &matrix, 0, nullptr, &mask, &mask.bounds,
                      0);
    require(pixels[10] == 128 && pixels[8] == 127, "Eight-bit matte did not blend correctly.");
    borrowed->pixels = original_pixels;
    borrowed->bounds = original_bounds;
    invoke(Selector::DisposeGWorld, address(borrowed));
    invoke(Selector::DisposeGWorld, address(source));
    invoke(Selector::DisposeGWorld, address(destination));
}

void verify_transparent_copy() {
    const auto source = create({3, 7, 5, 9});
    const auto destination = create({0, 0, 4, 4});
    const auto source_dc = reinterpret_cast<HDC>(invoke(Selector::GetPortHDC, address(source)));
    const auto destination_dc =
        reinterpret_cast<HDC>(invoke(Selector::GetPortHDC, address(destination)));
    const Color key{65535, 0, 65535};
    invoke(Selector::SetGWorld, address(source));
    invoke(Selector::RGBBackColor, address(&key));
    invoke(Selector::EraseRect, address(&source->bounds));
    SetPixel(source_dc, 8, 4, RGB(12, 34, 56));
    invoke(Selector::SetGWorld, address(destination));
    invoke(Selector::RGBBackColor, address(&key));
    invoke(Selector::PaintRect, address(&destination->bounds));
    const Rect clip{0, 0, 4, 3};
    invoke(Selector::ClipRect, address(&clip));
    invoke(Selector::CopyBits, address(*source->pixels), address(*destination->pixels),
           address(&source->bounds), address(&destination->bounds),
           static_cast<unsigned>(TransferMode::Transparent));
    require(GetPixel(destination_dc, 0, 0) == RGB(0, 0, 0),
            "Transparent source pixels overwrote the destination.");
    require(GetPixel(destination_dc, 2, 2) == RGB(12, 34, 56),
            "Opaque pixels did not scale into the destination.");
    invoke(Selector::ClipRect, address(&destination->bounds));
    require(GetPixel(destination_dc, 3, 2) == RGB(0, 0, 0),
            "Transparent copy ignored the destination clip.");
    invoke(Selector::DisposeGWorld, address(destination));
    invoke(Selector::DisposeGWorld, address(source));
}

}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto library = LoadLibraryW(argv[1]);
    if (!library) {
        return 1;
    }
    dispatcher = GetProcAddress(library, "theQuickTimeDispatcher");
    if (!dispatcher) {
        FreeLibrary(library);
        return 1;
    }

    int result = 0;
    try {
        invoke(Selector::QTMLInitInternals, 2);
        verify_pixels();
        verify_errors();
        verify_monochrome();
        verify_indexed();
        verify_callbacks();
        verify_drawing();
        verify_bgr_copy();
        verify_transparent_copy();
        verify_blended_copy();
        verify_matte();
        verify_window_port();

        const auto handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 100; ++i) {
            const auto port = create({0, 0, 60, 80});
            invoke(Selector::DisposeGWorld, address(port));
        }
        create({0, 0, 60, 80});
        invoke(Selector::QTMLTermInternals);
        require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == handles,
                "World allocation or termination leaked GDI objects.");
        std::cout << "GWorld ABI, pixel access, GDI drawing, errors and cleanup passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
        invoke(Selector::QTMLTermInternals);
    }

    FreeLibrary(library);
    return result;
}
