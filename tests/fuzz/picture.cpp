#include "picture/pict.h"

#include <cstddef>
#include <stdexcept>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        picture::read({data, size});
    } catch (const std::runtime_error&) {
    }
    return 0;
}
