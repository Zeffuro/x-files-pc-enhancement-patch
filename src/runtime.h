#pragma once

#include "entries.h"

#include <cstdint>

constexpr unsigned long unsupported_exit = 0x58510001;

Entry runtime_entry(Selector selector);

void trace_call(std::uint32_t selector, const char* name, std::uintptr_t caller);
void trace_value(const char* name, std::uint32_t value);

[[noreturn]] void unsupported(std::uint32_t selector, const char* name, std::uintptr_t caller);

[[noreturn]] inline void unsupported(Selector selector, const char* name, std::uintptr_t caller) {
    unsupported(static_cast<std::uint32_t>(selector), name, caller);
}
