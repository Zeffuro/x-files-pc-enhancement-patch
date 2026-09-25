#pragma once

#include <cstdint>

enum class Selector : std::uint32_t {
#define XFILES_SELECTOR(name, value) name = value,
#include "selectors.inc"
#undef XFILES_SELECTOR
};

inline const char* selector_name(Selector selector) {
    switch (selector) {
#define XFILES_SELECTOR(name, value)                                                               \
    case Selector::name:                                                                           \
        return #name;
#include "selectors.inc"
#undef XFILES_SELECTOR
        default:
            return "unknown";
    }
}
