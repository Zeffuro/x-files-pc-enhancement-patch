#include "memory.h"

#include <windows.h>
#include <algorithm>
#include <memory>
#include <unordered_map>

namespace {

using Handle = std::uint8_t**;

struct Allocation {
    std::uint8_t* pointer = nullptr;
    std::int32_t size = 0;
    std::uint8_t state = 0;

    ~Allocation() {
        if (pointer) {
            HeapFree(GetProcessHeap(), 0, pointer);
        }
    }
};

thread_local std::unordered_map<Handle, std::unique_ptr<Allocation>> handles;
thread_local short memory_error = 0;

Allocation* find(Handle handle) {
    const auto entry = handles.find(handle);
    memory_error = entry == handles.end() ? -109 : 0;
    return entry == handles.end() ? nullptr : entry->second.get();
}

template <bool clear> Handle __cdecl allocate(std::int32_t size) {
    if (size < 0) {
        memory_error = -50;
        return nullptr;
    }

    try {
        auto allocation = std::make_unique<Allocation>();
        const auto bytes = static_cast<SIZE_T>(std::max(size, 1));
        allocation->pointer = static_cast<std::uint8_t*>(
            HeapAlloc(GetProcessHeap(), clear ? HEAP_ZERO_MEMORY : 0, bytes));
        if (!allocation->pointer) {
            memory_error = -108;
            return nullptr;
        }
        allocation->size = size;
        Handle handle = &allocation->pointer;
        handles.emplace(handle, std::move(allocation));
        memory_error = 0;
        return handle;
    } catch (const std::bad_alloc&) {
        memory_error = -108;
        return nullptr;
    }
}

Handle __cdecl temporary_handle(std::int32_t size, short* error) {
    const auto handle = allocate<false>(size);
    if (error) {
        *error = memory_error;
    }
    return handle;
}

void __cdecl dispose(Handle handle) {
    memory_error = handle && !handles.erase(handle) ? -109 : 0;
}

std::int32_t __cdecl size_of(Handle handle) {
    const auto allocation = find(handle);
    return allocation ? allocation->size : 0;
}

void __cdecl resize(Handle handle, std::int32_t size) {
    const auto allocation = find(handle);
    if (!allocation) {
        return;
    }
    if (size < 0) {
        memory_error = -50;
        return;
    }

    const DWORD flags = allocation->state & 0x80 ? HEAP_REALLOC_IN_PLACE_ONLY : 0;
    const auto bytes = static_cast<SIZE_T>(std::max(size, 1));
    auto* replacement = static_cast<std::uint8_t*>(
        HeapReAlloc(GetProcessHeap(), flags, allocation->pointer, bytes));
    if (!replacement) {
        memory_error = -108;
        return;
    }
    allocation->pointer = replacement;
    allocation->size = size;
}

void __cdecl lock(Handle handle) {
    if (const auto allocation = find(handle)) {
        allocation->state |= 0x80;
    }
}

void __cdecl unlock(Handle handle) {
    if (const auto allocation = find(handle)) {
        allocation->state &= 0x7f;
    }
}

std::uint8_t __cdecl get_state(Handle handle) {
    const auto allocation = find(handle);
    return allocation ? allocation->state : 0;
}

void __cdecl set_state(Handle handle, std::uint8_t state) {
    if (const auto allocation = find(handle)) {
        allocation->state = state;
    }
}

short __cdecl last_error() {
    return memory_error;
}

}

Entry memory_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::MemError, last_error),
        bind_entry(Selector::NewHandle, allocate<false>),
        bind_entry(Selector::NewHandleSys, allocate<false>),
        bind_entry(Selector::NewHandleClear, allocate<true>),
        bind_entry(Selector::NewHandleSysClear, allocate<true>),
        bind_entry(Selector::HLock, lock),
        bind_entry(Selector::HLockHi, lock),
        bind_entry(Selector::HUnlock, unlock),
        bind_entry(Selector::TempNewHandle, temporary_handle),
        bind_entry(Selector::DisposeHandle, dispose),
        bind_entry(Selector::KillPicture, dispose),
        bind_entry(Selector::SetHandleSize, resize),
        bind_entry(Selector::GetHandleSize, size_of),
        bind_entry(Selector::HGetState, get_state),
        bind_entry(Selector::HSetState, set_state),
    };
    return find_entry(selector, entries);
}

std::span<const std::uint8_t> handle_bytes(std::uint8_t** handle) {
    const auto found = handles.find(handle);
    if (found == handles.end()) {
        return {};
    }
    const auto& allocation = *found->second;
    return {allocation.pointer, static_cast<std::size_t>(allocation.size)};
}

void release_handles() {
    handles.clear();
    memory_error = 0;
}
