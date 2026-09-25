#include "imports.h"

#include <cstring>
#include <cstdlib>

namespace {

bool replace(ULONG_PTR* slot, ULONG_PTR value) {
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
        return false;
    }
    *slot = value;
    DWORD unused = 0;
    return VirtualProtect(slot, sizeof(*slot), protection, &unused) != FALSE;
}

}

bool ImportHooks::install(HMODULE executable, const char* library, Resolver resolve) {
    if (count_) {
        return false;
    }
    auto* base = reinterpret_cast<BYTE*>(executable);
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* pe = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        return false;
    }
    const auto directory = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) {
        return false;
    }
    const auto* descriptor =
        reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), library) ||
            !descriptor->OriginalFirstThunk) {
            continue;
        }
        const auto* names =
            reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) {
                continue;
            }
            const auto* name =
                reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            const auto hook = resolve(name->Name);
            if (!hook) {
                continue;
            }
            if (count_ == _countof(imports_)) {
                remove();
                return false;
            }
            imports_[count_++] = {&slots->u1.Function, slots->u1.Function,
                                  reinterpret_cast<ULONG_PTR>(hook)};
            if (!replace(&slots->u1.Function, reinterpret_cast<ULONG_PTR>(hook))) {
                remove();
                return false;
            }
        }
    }
    return count_ != 0;
}

FARPROC ImportHooks::previous(FARPROC hook) const {
    for (unsigned index = 0; index < count_; ++index) {
        if (imports_[index].replacement == reinterpret_cast<ULONG_PTR>(hook)) {
            return reinterpret_cast<FARPROC>(imports_[index].original);
        }
    }
    return nullptr;
}

bool ImportHooks::active() const {
    for (unsigned index = 0; index < count_; ++index) {
        if (*imports_[index].slot != imports_[index].replacement) {
            return false;
        }
    }
    return count_ != 0;
}

void ImportHooks::remove() {
    while (count_) {
        const auto& import = imports_[--count_];
        if (*import.slot == import.replacement) {
            replace(import.slot, import.original);
        }
    }
}
