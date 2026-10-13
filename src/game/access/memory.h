#pragma once

#include "game/profiles/generated.h"
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace native_game {
template <class T> class MemoryRef {
public:
    explicit MemoryRef(const void* object = nullptr, const Profile* build = nullptr)
        : location_(reinterpret_cast<std::uintptr_t>(object)), profile_(build) {}

    explicit MemoryRef(std::uintptr_t object, const Profile* build = nullptr)
        : location_(object), profile_(build) {}

    std::uintptr_t address() const {
        return location_;
    }

    std::byte* data() const {
        return reinterpret_cast<std::byte*>(location_);
    }

    T* get() const {
        return reinterpret_cast<T*>(location_);
    }

    const Profile* profile() const {
        return profile_;
    }

    template <class U> bool read(U& value) const {
        static_assert(sizeof(T) == sizeof(U));
        static_assert(std::is_trivially_copyable_v<U>);
        SIZE_T copied = 0;
        return location_ &&
               ReadProcessMemory(GetCurrentProcess(), data(), &value, sizeof(value), &copied) &&
               copied == sizeof(value);
    }

protected:
    template <class U> MemoryRef<U> field(std::size_t offset) const {
        return MemoryRef<U>(offset_address(offset), profile_);
    }

    std::uintptr_t offset_address(std::size_t offset) const {
        return location_ && offset <= std::numeric_limits<std::uintptr_t>::max() - location_
                   ? location_ + offset
                   : 0;
    }

    std::uintptr_t before_address(std::size_t offset) const {
        return location_ >= offset ? location_ - offset : 0;
    }

private:
    std::uintptr_t location_;
    const Profile* profile_;
};
}
