#include <cstdint>
#include <iostream>
#include <string>
#include <windows.h>

namespace {

FARPROC dispatcher;

__declspec(naked) std::uint32_t __cdecl invoke(std::uint32_t, std::uint32_t) {
    __asm {
        mov eax, [esp + 4]
        push dword ptr [esp + 8]
        call dword ptr [dispatcher]
        add esp, 4
        ret
    }
}

__declspec(naked) unsigned __cdecl preserved_registers() {
    __asm {
        push ebx
        push esi
        push edi
        mov ebx, 13579bdfh
        mov esi, 2468ace0h
        mov edi, 12345678h
        push 0
        mov eax, 1d0008h
        call dword ptr [dispatcher]
        add esp, 4
        cmp ebx, 13579bdfh
        jne failed
        cmp esi, 2468ace0h
        jne failed
        cmp edi, 12345678h
        jne failed
        mov eax, 1
        jmp done
    failed:
        xor eax, eax
    done:
        pop edi
        pop esi
        pop ebx
        ret
    }
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) {
        return 1;
    }
    HMODULE library = LoadLibraryW(argv[1]);
    if (!library) {
        return 2;
    }
    dispatcher = GetProcAddress(library, "theQuickTimeDispatcher");
    if (!dispatcher || !GetProcAddress(library, "_CallComponent") ||
        !GetProcAddress(library, "_CallComponentFunctionWithStorage")) {
        return 3;
    }
    if (argc == 3) {
        invoke(0xf00d1234, 0);
        return 9;
    }
    if (static_cast<short>(invoke(0x20001, 0)) != -50) {
        return 4;
    }
    if (static_cast<short>(invoke(0x1d0008, 0x7fffffff)) != -50) {
        return 5;
    }
    for (unsigned i = 0; i < 10000; ++i) {
        if (static_cast<short>(invoke(0x1d0008, 0)) != 0) {
            return 6;
        }
        if (static_cast<short>(invoke(0x20001, 0)) != 0) {
            return 7;
        }
        invoke(0x20002, 0);
        invoke(0x1d0009, 0);
    }
    if (static_cast<short>(invoke(0x20001, 0)) != -50) {
        return 8;
    }
    if (!preserved_registers()) {
        return 10;
    }
    invoke(0x1d0009, 0);
    FreeLibrary(library);
    std::wstring command = L"\"" + std::wstring(argv[0]) + L"\" \"" + argv[1] + L"\" --unsupported";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &startup, &child)) {
        return 11;
    }
    const auto wait = WaitForSingleObject(child.hProcess, 5000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(child.hProcess, 1);
        WaitForSingleObject(child.hProcess, 5000);
    }
    DWORD result = 0;
    const auto read = GetExitCodeProcess(child.hProcess, &result);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    if (!read || result != 0x58510001) {
        return 12;
    }
    std::cout
        << "Exports, stack, nonvolatile registers, lifecycle and unsupported-call exit passed.\n";
    return 0;
}
