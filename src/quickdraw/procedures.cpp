#include "world.h"
#include "matrix.h"
#include <intrin.h>

namespace {

using namespace quickdraw;

void __cdecl rect_matrix(Matrix* output, const Rect* source, const Rect* destination) {
    if (!output || !source || !destination) {
        return;
    }
    try {
        *output = rectangle_matrix(*source, *destination);
    } catch (const std::exception& error) {
        unsupported(Selector::RectMatrix, error.what(), 0);
    }
}

template <unsigned slot> [[noreturn]] void __cdecl missing_procedure() {
    constexpr const char* names[] = {
        "StdText",   "StdLine",   "StdRect", "StdRRect",   "StdOval",         "StdArc",
        "StdPoly",   "StdRgn",    "StdBits", "StdComment", "StdTxMeas",       "StdGetPic",
        "StdPutPic", "StdOpcode", "StdPix",  "StdGlyphs",  "StdPrinterStatus"};
    unsupported(Selector::SetStdCProcs, names[slot],
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}

void __cdecl standard_procedures(Procedures* output) {
    if (!output) {
        unsupported(Selector::SetStdCProcs, "SetStdCProcs: missing table", 0);
    }
    *output = {};
    output->text = missing_procedure<0>;
    output->line = missing_procedure<1>;
    output->rectangle = standard_rectangle;
    output->rounded_rectangle = missing_procedure<3>;
    output->oval = missing_procedure<4>;
    output->arc = missing_procedure<5>;
    output->polygon = missing_procedure<6>;
    output->region = missing_procedure<7>;
    output->bits = standard_bits;
    output->comment = missing_procedure<9>;
    output->text_measurement = missing_procedure<10>;
    output->get_picture = missing_procedure<11>;
    output->put_picture = missing_procedure<12>;
    output->opcode = missing_procedure<13>;
    output->pixels = standard_pixels;
    output->glyphs = missing_procedure<15>;
    output->printer_status = missing_procedure<16>;
}

}

Entry procedures_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::SetStdCProcs, standard_procedures),
        bind_entry(Selector::RectMatrix, rect_matrix),
    };
    return find_entry(selector, entries);
}
