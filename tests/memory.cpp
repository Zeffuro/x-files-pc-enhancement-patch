#include "dispatch.h"

#include <algorithm>
#include <iostream>

namespace {

using namespace test;
using Handle = std::uint8_t**;

short memory_error() {
    return static_cast<short>(invoke(Selector::MemError));
}

void verify_handles() {
    const auto handle = reinterpret_cast<Handle>(invoke(Selector::NewHandleClear, 16));
    require(handle && *handle && memory_error() == 0, "NewHandleClear failed.");
    require(std::all_of(*handle, *handle + 16, [](auto byte) { return byte == 0; }),
            "NewHandleClear did not clear its storage.");
    for (std::uint8_t i = 0; i < 16; ++i) {
        (*handle)[i] = i;
    }
    invoke(Selector::SetHandleSize, address(handle), 4096);
    require(memory_error() == 0 && invoke(Selector::GetHandleSize, address(handle)) == 4096,
            "SetHandleSize failed to grow the allocation.");
    for (std::uint8_t i = 0; i < 16; ++i) {
        require((*handle)[i] == i, "Resizing lost the original data.");
    }

    invoke(Selector::HSetState, address(handle), 0x40);
    invoke(Selector::HLock, address(handle));
    require((invoke(Selector::HGetState, address(handle)) & 0xff) == 0xc0,
            "HLock discarded other handle flags.");
    const auto locked = *handle;
    invoke(Selector::SetHandleSize, address(handle), 8192);
    require(*handle == locked, "Resizing moved a locked handle.");
    const auto error = memory_error();
    require(error == 0 || error == -108, "Locked resize returned an unexpected error.");
    require(invoke(Selector::GetHandleSize, address(handle)) == (error ? 4096u : 8192u),
            "Locked resize did not preserve size on failure.");
    invoke(Selector::HUnlock, address(handle));
    require((invoke(Selector::HGetState, address(handle)) & 0xff) == 0x40,
            "HUnlock discarded other handle flags.");

    invoke(Selector::SetHandleSize, address(handle), static_cast<std::uintptr_t>(-1));
    require(memory_error() == -50 && *handle == locked, "Negative resize changed the allocation.");
    invoke(Selector::SetHandleSize, address(handle), 0);
    require(memory_error() == 0 && invoke(Selector::GetHandleSize, address(handle)) == 0,
            "Zero-length handles are not supported.");
    invoke(Selector::DisposeHandle, address(handle));
    invoke(Selector::GetHandleSize, address(handle));
    require(memory_error() == -109, "A disposed handle was accepted.");

    short temporary_error = 0;
    require(invoke(Selector::TempNewHandle, static_cast<std::uintptr_t>(-1),
                   address(&temporary_error)) == 0 &&
                temporary_error == -50,
            "TempNewHandle did not report its allocation error.");
}

}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto library = LoadLibraryW(argv[1]);
    if (!library) {
        return 1;
    }
    dispatcher = GetProcAddress(library, "theQuickTimeDispatcher");
    if (!dispatcher) {
        FreeLibrary(library);
        return 1;
    }
    int result = 0;
    try {
        invoke(Selector::QTMLInitInternals, 2);
        verify_handles();
        const auto leftover = invoke(Selector::NewHandle, 32);
        invoke(Selector::QTMLTermInternals);
        invoke(Selector::GetHandleSize, leftover);
        require(memory_error() == -109, "Termination left an allocation registered.");
        std::cout << "Handle ABI, storage, locking, errors and cleanup passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
        invoke(Selector::QTMLTermInternals);
    }
    FreeLibrary(library);
    return result;
}
