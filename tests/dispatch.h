#pragma once

#include "selectors.h"

#include <windows.h>
#include <cstdint>
#include <stdexcept>

namespace test {

inline FARPROC dispatcher;

__declspec(naked) inline std::uint32_t __cdecl invoke(Selector, std::uintptr_t = 0,
                                                      std::uintptr_t = 0, std::uintptr_t = 0,
                                                      std::uintptr_t = 0, std::uintptr_t = 0,
                                                      std::uintptr_t = 0, std::uintptr_t = 0) {
    __asm {
        mov eax, [esp + 4]
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        call dword ptr [dispatcher]
        add esp, 28
        ret
    }
}

template <typename T> std::uintptr_t address(T* pointer) {
    return reinterpret_cast<std::uintptr_t>(pointer);
}

inline void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

}
