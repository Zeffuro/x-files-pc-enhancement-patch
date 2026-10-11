#include "imports.h"

#include <cstring>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

bool readable_protection(DWORD protection) {
    if (protection & (PAGE_GUARD | PAGE_NOACCESS)) {
        return false;
    }
    switch (protection & 0xff) {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
    }
}

class Image {
    std::uintptr_t base_;
    std::size_t allocation_ = 0;
    std::size_t size_ = 0;

public:
    explicit Image(HMODULE module) : base_(reinterpret_cast<std::uintptr_t>(module)) {
        MEMORY_BASIC_INFORMATION region{};
        if (!base_ || !VirtualQuery(module, &region, sizeof(region)) ||
            reinterpret_cast<std::uintptr_t>(region.AllocationBase) != base_) {
            return;
        }
        auto address = base_;
        do {
            const auto start = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            if (start != address || !region.RegionSize ||
                region.RegionSize > std::numeric_limits<std::uintptr_t>::max() - address) {
                return;
            }
            address += region.RegionSize;
        } while (VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region)) &&
                 reinterpret_cast<std::uintptr_t>(region.AllocationBase) == base_);
        allocation_ = address - base_;
        size_ = allocation_;
    }

    bool contains(std::size_t offset, std::size_t bytes, std::size_t alignment = 1) const {
        return allocation_ && offset <= size_ && bytes <= size_ - offset && offset % alignment == 0;
    }

    std::size_t readable(std::size_t offset) const {
        if (!contains(offset, 1)) {
            return 0;
        }
        MEMORY_BASIC_INFORMATION region{};
        const auto address = base_ + offset;
        if (!VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region)) ||
            reinterpret_cast<std::uintptr_t>(region.AllocationBase) != base_ ||
            region.State != MEM_COMMIT || !readable_protection(region.Protect)) {
            return 0;
        }
        const auto within = address - reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        return std::min<std::size_t>(region.RegionSize - within, size_ - offset);
    }

    bool range(std::size_t offset, std::size_t bytes, std::size_t alignment = 1) const {
        if (!contains(offset, bytes, alignment)) {
            return false;
        }
        while (bytes) {
            const auto count = std::min(readable(offset), bytes);
            if (!count) {
                return false;
            }
            offset += count;
            bytes -= count;
        }
        return true;
    }

    template <class T> bool read(std::size_t offset, T& value) const {
        if (!range(offset, sizeof(T), alignof(T))) {
            return false;
        }
        std::memcpy(&value, pointer(offset), sizeof(T));
        return true;
    }

    BYTE* pointer(std::size_t offset) const {
        return reinterpret_cast<BYTE*>(base_ + offset);
    }

    const char* string(std::size_t offset) const {
        const auto first = offset;
        while (const auto count = readable(offset)) {
            const auto start = reinterpret_cast<const char*>(pointer(offset));
            if (std::memchr(start, '\0', count)) {
                return reinterpret_cast<const char*>(pointer(first));
            }
            offset += count;
        }
        return nullptr;
    }

    bool directory(IMAGE_DATA_DIRECTORY& directory) {
        IMAGE_DOS_HEADER dos{};
        if (!read(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew < static_cast<LONG>(sizeof(dos))) {
            return false;
        }
        const auto pe_offset = static_cast<std::size_t>(dos.e_lfanew);
        DWORD signature = 0;
        IMAGE_FILE_HEADER file{};
        if (!read(pe_offset, signature) || signature != IMAGE_NT_SIGNATURE ||
            !read(pe_offset + sizeof(signature), file) || file.Machine != IMAGE_FILE_MACHINE_I386) {
            return false;
        }
        const auto optional_offset = pe_offset + sizeof(signature) + sizeof(file);
        constexpr auto directory_offset = offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
        constexpr auto required =
            directory_offset + (IMAGE_DIRECTORY_ENTRY_IMPORT + 1) * sizeof(directory);
        IMAGE_OPTIONAL_HEADER32 optional{};
        if (file.SizeOfOptionalHeader < required ||
            !range(optional_offset, file.SizeOfOptionalHeader, alignof(DWORD)) ||
            !range(optional_offset, required)) {
            return false;
        }
        std::memcpy(&optional, pointer(optional_offset), required);
        if (optional.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            optional.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT ||
            optional.NumberOfRvaAndSizes > IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
            optional.NumberOfRvaAndSizes >
                (file.SizeOfOptionalHeader - directory_offset) / sizeof(directory) ||
            optional.SizeOfImage > allocation_ ||
            optional.SizeOfImage < optional_offset + file.SizeOfOptionalHeader) {
            return false;
        }
        size_ = optional.SizeOfImage;
        directory = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        return directory.VirtualAddress && directory.Size >= sizeof(IMAGE_IMPORT_DESCRIPTOR) &&
               contains(directory.VirtualAddress, directory.Size, alignof(DWORD));
    }
};

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
    if (count_ || !library || !resolve) {
        return false;
    }
    Image image(executable);
    IMAGE_DATA_DIRECTORY directory{};
    if (!image.directory(directory)) {
        return false;
    }
    std::array<Import, 16> candidates{};
    unsigned count = 0;
    bool terminated = false;
    for (std::size_t offset = 0; sizeof(IMAGE_IMPORT_DESCRIPTOR) <= directory.Size - offset;
         offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        if (!image.read(directory.VirtualAddress + offset, descriptor)) {
            return false;
        }
        if (!descriptor.Name) {
            if (descriptor.OriginalFirstThunk || descriptor.TimeDateStamp ||
                descriptor.ForwarderChain || descriptor.FirstThunk) {
                return false;
            }
            terminated = true;
            break;
        }
        const auto library_name = image.string(descriptor.Name);
        if (!library_name || !*library_name) {
            return false;
        }
        if (_stricmp(library_name, library) || !descriptor.OriginalFirstThunk) {
            continue;
        }
        if (!descriptor.FirstThunk) {
            return false;
        }
        auto names = static_cast<std::size_t>(descriptor.OriginalFirstThunk);
        auto slots = static_cast<std::size_t>(descriptor.FirstThunk);
        for (;;) {
            IMAGE_THUNK_DATA32 name{}, slot{};
            if (!image.read(names, name) || !image.read(slots, slot)) {
                return false;
            }
            if (!name.u1.AddressOfData) {
                if (slot.u1.Function) {
                    return false;
                }
                break;
            }
            if (!slot.u1.Function) {
                return false;
            }
            names += sizeof(name);
            const auto slot_offset = slots;
            slots += sizeof(slot);
            if (IMAGE_SNAP_BY_ORDINAL32(name.u1.Ordinal)) {
                continue;
            }
            WORD hint = 0;
            if (!image.read(name.u1.AddressOfData, hint)) {
                return false;
            }
            const auto function =
                image.string(static_cast<std::size_t>(name.u1.AddressOfData) + sizeof(hint));
            if (!function || !*function) {
                return false;
            }
            const auto hook = resolve(function);
            if (!hook) {
                continue;
            }
            if (count == candidates.size()) {
                return false;
            }
            auto* target = reinterpret_cast<ULONG_PTR*>(image.pointer(slot_offset));
            for (unsigned index = 0; index < count; ++index) {
                if (candidates[index].slot == target) {
                    return false;
                }
            }
            candidates[count++] = {target, slot.u1.Function, reinterpret_cast<ULONG_PTR>(hook)};
        }
    }
    if (!terminated) {
        return false;
    }
    for (unsigned index = 0; index < count; ++index) {
        imports_[count_++] = candidates[index];
        if (!replace(candidates[index].slot, candidates[index].replacement)) {
            remove();
            return false;
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
