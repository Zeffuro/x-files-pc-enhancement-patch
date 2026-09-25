#include "runtime.h"
#include "selectors.h"
#include "playback/components.h"

#include <intrin.h>

extern "C" Entry __cdecl resolve_entry(std::uint32_t selector, std::uintptr_t caller) {
    const auto operation = static_cast<Selector>(selector);
    const char* name = selector_name(operation);
    trace_call(selector, name, caller);
    if (Entry entry = runtime_entry(operation)) {
        return entry;
    }
    unsupported(selector, name, caller);
}

// SDK thunks tail-jump here with the selector in EAX and cdecl arguments intact.
extern "C" __declspec(naked) void theQuickTimeDispatcher() {
    __asm {
        pushfd
        pushad
        push dword ptr [esp + 36]
        push eax
        call resolve_entry
        add esp, 8
        mov dword ptr [esp + 28], eax
        popad
        popfd
        jmp eax
    }
}

extern "C" std::int32_t __cdecl CallComponent(void* component,
                                              const playback::ComponentParameters* parameters) {
    return playback::call_component(component, parameters);
}

extern "C" void __cdecl CallComponentFunctionWithStorage() {
    unsupported(0, "_CallComponentFunctionWithStorage",
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
