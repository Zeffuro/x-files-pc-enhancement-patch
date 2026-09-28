#include "saves/database.h"

#include <cstddef>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        const saves::database::Container container({data, data + size});
    } catch (const std::runtime_error&) {
    }
    return 0;
}
