#pragma once

#include "selectors.h"

#include <span>

using Entry = void(__cdecl*)();

struct EntryBinding {
    Selector selector;
    Entry function;
};

template <typename Result, typename... Args>
EntryBinding bind_entry(Selector selector, Result(__cdecl* function)(Args...)) {
    return {selector, reinterpret_cast<Entry>(function)};
}

inline Entry find_entry(Selector selector, std::span<const EntryBinding> entries) {
    for (const auto& entry : entries) {
        if (entry.selector == selector) {
            return entry.function;
        }
    }
    return nullptr;
}
