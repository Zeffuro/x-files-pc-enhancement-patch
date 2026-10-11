#include "platform/imports.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

INT_PTR WINAPI hook() {
    return 1;
}

FARPROC resolve(const char* name) {
    return std::strcmp(name, "HookMe") == 0 ? &hook : nullptr;
}

class Fixture {
public:
    static constexpr DWORD pe_offset = 0x80, directory_offset = 0x200;
    static constexpr DWORD names_offset = 0x400, slots_offset = 0x800;
    static constexpr DWORD library_offset = 0x1000, function_offset = 0x1200;
    BYTE* base = nullptr;
    DWORD page = 0, size = 0;

    Fixture() {
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        page = system.dwPageSize;
        size = page * 6;
        base = static_cast<BYTE*>(
            VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        require(base != nullptr, "Cannot allocate import fixture");
        IMAGE_DOS_HEADER dos{};
        dos.e_magic = IMAGE_DOS_SIGNATURE;
        dos.e_lfanew = pe_offset;
        put(0, dos);
        IMAGE_NT_HEADERS32 pe{};
        pe.Signature = IMAGE_NT_SIGNATURE;
        pe.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
        pe.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
        pe.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        pe.OptionalHeader.SizeOfImage = size;
        pe.OptionalHeader.SizeOfHeaders = page;
        pe.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        pe.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {
            directory_offset, 2 * sizeof(IMAGE_IMPORT_DESCRIPTOR)};
        put(pe_offset, pe);
        descriptor().Name = library_offset;
        descriptor().OriginalFirstThunk = names_offset;
        descriptor().FirstThunk = slots_offset;
        text(library_offset, "kernel32.dll");
        text(function_offset + sizeof(WORD), "HookMe");
        names()[0].u1.AddressOfData = function_offset;
        slots()[0].u1.Function = 0x12345678;
    }

    ~Fixture() {
        VirtualFree(base, 0, MEM_RELEASE);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    HMODULE module() const {
        return reinterpret_cast<HMODULE>(base);
    }

    template <class T> void put(DWORD offset, const T& value) {
        std::memcpy(base + offset, &value, sizeof(value));
    }

    void text(DWORD offset, const char* value) {
        std::memcpy(base + offset, value, std::strlen(value) + 1);
    }

    IMAGE_DOS_HEADER& dos() {
        return *reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    }

    IMAGE_NT_HEADERS32& pe() {
        return *reinterpret_cast<IMAGE_NT_HEADERS32*>(base + pe_offset);
    }

    IMAGE_DATA_DIRECTORY& directory() {
        return pe().OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    }

    IMAGE_IMPORT_DESCRIPTOR& descriptor(unsigned index = 0) {
        return reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory_offset)[index];
    }

    IMAGE_THUNK_DATA32* names() {
        return reinterpret_cast<IMAGE_THUNK_DATA32*>(base + names_offset);
    }

    IMAGE_THUNK_DATA32* slots() {
        return reinterpret_cast<IMAGE_THUNK_DATA32*>(base + slots_offset);
    }

    void reject() {
        std::array<IMAGE_THUNK_DATA32, 20> original{};
        std::memcpy(original.data(), slots(), sizeof(original));
        ImportHooks imports;
        require(!imports.install(module(), "KERNEL32.DLL", resolve),
                "Malformed import image accepted");
        require(!imports.active() && !imports.previous(&hook), "Rejected image retained a hook");
        require(std::memcmp(original.data(), slots(), sizeof(original)) == 0,
                "Rejected image changed import slots");
    }

    void inaccessible(DWORD offset, DWORD protection) {
        DWORD previous = 0;
        require(VirtualProtect(base + offset, page, protection, &previous) != FALSE,
                "Cannot protect fixture page");
        ImportHooks imports;
        const auto accepted = imports.install(module(), "kernel32.dll", resolve);
        DWORD ignored = 0;
        require(VirtualProtect(base + offset, page, previous, &ignored) != FALSE,
                "Cannot restore fixture protection");
        require(!accepted && !imports.active() && !imports.previous(&hook),
                "Unreadable fixture accepted");
        require(slots()[0].u1.Function == 0x12345678,
                "Unreadable fixture changed an earlier candidate");
    }
};

void valid_images() {
    Fixture fixture;
    ImportHooks imports;
    DWORD previous = 0;
    require(VirtualProtect(fixture.base, fixture.size, PAGE_READONLY, &previous) != FALSE,
            "Cannot make fixture read-only");
    require(imports.install(fixture.module(), "KERNEL32.DLL", resolve) && imports.active(),
            "Valid read-only image rejected");
    require(fixture.slots()[0].u1.Function == reinterpret_cast<ULONG_PTR>(&hook) &&
                imports.previous(&hook) == reinterpret_cast<FARPROC>(0x12345678),
            "Hook replacement or original import lost");
    require(!imports.install(fixture.module(), "kernel32.dll", resolve) && imports.active(),
            "Repeated install changed active hooks");
    MEMORY_BASIC_INFORMATION region{};
    require(VirtualQuery(fixture.slots(), &region, sizeof(region)) &&
                region.Protect == PAGE_READONLY,
            "Hook installation changed page protection");
    imports.remove();
    require(!imports.active() && !imports.previous(&hook) &&
                fixture.slots()[0].u1.Function == 0x12345678,
            "Removal did not restore original import");
    imports.remove();
    DWORD ignored = 0;
    require(VirtualProtect(fixture.base, fixture.size, previous, &ignored) != FALSE,
            "Cannot restore writable fixture");

    require(imports.install(fixture.module(), "kernel32.dll", resolve), "Reinstall failed");
    fixture.slots()[0].u1.Function = 0x87654321;
    require(!imports.active(), "Third-party replacement remained active");
    imports.remove();
    require(fixture.slots()[0].u1.Function == 0x87654321 && !imports.previous(&hook),
            "Removal overwrote a third-party replacement");

    Fixture ordinal;
    ordinal.names()[0].u1.Ordinal = IMAGE_ORDINAL_FLAG32 | 7;
    ordinal.names()[1].u1.AddressOfData = Fixture::function_offset;
    ordinal.slots()[1].u1.Function = 0x23456789;
    require(imports.install(ordinal.module(), "kernel32.dll", resolve) &&
                ordinal.slots()[0].u1.Function == 0x12345678 &&
                ordinal.slots()[1].u1.Function == reinterpret_cast<ULONG_PTR>(&hook),
            "Ordinal import was replaced or hid the named import");
    imports.remove();
    require(ordinal.slots()[1].u1.Function == 0x23456789,
            "Named import after ordinal was not restored");

    Fixture crossing;
    crossing.descriptor().Name = crossing.page - 4;
    crossing.text(crossing.page - 4, "kernel32.dll");
    crossing.names()[0].u1.AddressOfData = crossing.page * 3 - 4;
    crossing.text(crossing.page * 3 - 2, "HookMe");
    crossing.pe().FileHeader.SizeOfOptionalHeader =
        offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory) + 2 * sizeof(IMAGE_DATA_DIRECTORY);
    crossing.pe().OptionalHeader.NumberOfRvaAndSizes = 2;
    require(imports.install(crossing.module(), "kernel32.dll", resolve),
            "Readable cross-page strings or shorter valid optional header rejected");
    imports.remove();

    Fixture full;
    for (unsigned index = 0; index < 16; ++index) {
        full.names()[index].u1.AddressOfData = Fixture::function_offset;
        full.slots()[index].u1.Function = 0x12345678 + index;
    }
    require(imports.install(full.module(), "kernel32.dll", resolve),
            "Sixteen import hooks rejected");
    imports.remove();
    for (unsigned index = 0; index < 16; ++index) {
        require(full.slots()[index].u1.Function == 0x12345678 + index,
                "Capacity-sized import set was not restored");
    }
}

void malformed_images() {
    using Mutation = std::pair<const char*, std::function<void(Fixture&)>>;
    const std::vector<Mutation> cases{
        {"DOS signature", [](auto& f) { f.dos().e_magic = 0; }},
        {"negative PE offset", [](auto& f) { f.dos().e_lfanew = -1; }},
        {"overlapping PE header", [](auto& f) { f.dos().e_lfanew = 0; }},
        {"unaligned PE header", [](auto& f) { ++f.dos().e_lfanew; }},
        {"PE offset outside allocation", [](auto& f) { f.dos().e_lfanew = 0x7ffffffc; }},
        {"truncated PE header", [](auto& f) { f.dos().e_lfanew = f.size - 4; }},
        {"PE signature", [](auto& f) { f.pe().Signature = 0; }},
        {"machine type", [](auto& f) { f.pe().FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64; }},
        {"PE32 magic",
         [](auto& f) { f.pe().OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC; }},
        {"short optional header", [](auto& f) { f.pe().FileHeader.SizeOfOptionalHeader = 96; }},
        {"optional header outside allocation",
         [](auto& f) {
             f.pe().FileHeader.SizeOfOptionalHeader = std::numeric_limits<WORD>::max();
         }},
        {"missing directory count", [](auto& f) { f.pe().OptionalHeader.NumberOfRvaAndSizes = 1; }},
        {"excess directory count", [](auto& f) { f.pe().OptionalHeader.NumberOfRvaAndSizes = 17; }},
        {"directory count outside header",
         [](auto& f) { f.pe().FileHeader.SizeOfOptionalHeader = 112; }},
        {"zero image size", [](auto& f) { f.pe().OptionalHeader.SizeOfImage = 0; }},
        {"image larger than allocation",
         [](auto& f) { f.pe().OptionalHeader.SizeOfImage = f.size + 1; }},
        {"headers outside image", [](auto& f) { f.pe().OptionalHeader.SizeOfImage = 0x100; }},
        {"absent imports", [](auto& f) { f.directory() = {}; }},
        {"short import directory", [](auto& f) { f.directory().Size = 19; }},
        {"import directory outside image", [](auto& f) { f.directory().VirtualAddress = f.size; }},
        {"wrapping import extent", [](auto& f) { f.directory().Size = 0xffffffff; }},
        {"unaligned import directory", [](auto& f) { ++f.directory().VirtualAddress; }},
        {"missing descriptor terminator", [](auto& f) { f.directory().Size = 20; }},
        {"nonzero descriptor terminator", [](auto& f) { f.descriptor(1).FirstThunk = 1; }},
        {"later malformed descriptor", [](auto& f) { f.descriptor(1).Name = f.size; }},
        {"library outside image", [](auto& f) { f.descriptor().Name = f.size; }},
        {"unterminated library",
         [](auto& f) {
             f.descriptor().Name = f.size - 4;
             std::memset(f.base + f.size - 4, 'x', 4);
         }},
        {"empty library", [](auto& f) { f.base[Fixture::library_offset] = 0; }},
        {"name table outside image", [](auto& f) { f.descriptor().OriginalFirstThunk = f.size; }},
        {"unaligned name table", [](auto& f) { ++f.descriptor().OriginalFirstThunk; }},
        {"absent slot table", [](auto& f) { f.descriptor().FirstThunk = 0; }},
        {"slot table outside image", [](auto& f) { f.descriptor().FirstThunk = f.size; }},
        {"unaligned slot table", [](auto& f) { ++f.descriptor().FirstThunk; }},
        {"unterminated name table",
         [](auto& f) {
             f.descriptor().OriginalFirstThunk = f.size - 4;
             f.put(f.size - 4, IMAGE_THUNK_DATA32{Fixture::function_offset});
         }},
        {"unterminated slot table",
         [](auto& f) {
             f.descriptor().FirstThunk = f.size - 4;
             f.put(f.size - 4, IMAGE_THUNK_DATA32{0x12345678});
         }},
        {"nonzero slot terminator", [](auto& f) { f.slots()[1].u1.Function = 1; }},
        {"premature slot terminator", [](auto& f) { f.slots()[0].u1.Function = 0; }},
        {"hint outside image", [](auto& f) { f.names()[0].u1.AddressOfData = f.size; }},
        {"unaligned hint", [](auto& f) { ++f.names()[0].u1.AddressOfData; }},
        {"truncated hint", [](auto& f) { f.names()[0].u1.AddressOfData = f.size - 1; }},
        {"function string outside image",
         [](auto& f) { f.names()[0].u1.AddressOfData = f.size - 2; }},
        {"unterminated function",
         [](auto& f) {
             f.names()[0].u1.AddressOfData = f.size - 4;
             f.base[f.size - 2] = 'x';
             f.base[f.size - 1] = 'x';
         }},
        {"empty function", [](auto& f) { f.base[Fixture::function_offset + 2] = 0; }},
        {"later malformed function",
         [](auto& f) {
             f.names()[1].u1.AddressOfData = f.size;
             f.slots()[1].u1.Function = 0x23456789;
         }},
        {"seventeen hooks",
         [](auto& f) {
             for (unsigned index = 0; index < 17; ++index) {
                 f.names()[index].u1.AddressOfData = Fixture::function_offset;
                 f.slots()[index].u1.Function = 0x12345678 + index;
             }
         }},
        {"duplicate slot descriptors",
         [](auto& f) {
             f.descriptor(1) = f.descriptor();
             f.directory().Size = 3 * sizeof(IMAGE_IMPORT_DESCRIPTOR);
         }},
        {"image bound smaller than allocation",
         [](auto& f) { f.pe().OptionalHeader.SizeOfImage = Fixture::function_offset; }},
    };
    for (const auto& [name, mutate] : cases) {
        Fixture fixture;
        mutate(fixture);
        try {
            fixture.reject();
        } catch (const std::exception&) {
            std::cerr << "Failed fixture: " << name << '\n';
            throw;
        }
    }
}

void unreadable_images() {
    for (const auto protection : {PAGE_NOACCESS, PAGE_READWRITE | PAGE_GUARD, PAGE_EXECUTE}) {
        Fixture header;
        header.inaccessible(0, protection);
        Fixture library;
        library.inaccessible(library.page, protection);
        Fixture later;
        later.names()[1].u1.AddressOfData = later.page * 3;
        later.slots()[1].u1.Function = 0x23456789;
        later.text(later.page * 3 + 2, "HookMe");
        later.inaccessible(later.page * 3, protection);
    }
    Fixture crossing;
    crossing.descriptor().Name = crossing.page * 3 - 4;
    std::memset(crossing.base + crossing.page * 3 - 4, 'x', 4);
    crossing.inaccessible(crossing.page * 3, PAGE_NOACCESS);

    Fixture reserved;
    reserved.names()[1].u1.AddressOfData = reserved.page * 3;
    reserved.slots()[1].u1.Function = 0x23456789;
    require(VirtualFree(reserved.base + reserved.page * 3, reserved.page, MEM_DECOMMIT) != FALSE,
            "Cannot decommit fixture page");
    reserved.reject();

    Fixture boundary;
    boundary.dos().e_lfanew = boundary.page - 4;
    boundary.put(boundary.page - 4, DWORD{IMAGE_NT_SIGNATURE});
    boundary.inaccessible(boundary.page, PAGE_NOACCESS);
}

void skipped_and_missing() {
    Fixture fixture;
    ImportHooks imports;
    require(!imports.install(nullptr, "kernel32.dll", resolve), "Null module accepted");
    require(!imports.install(fixture.module(), nullptr, resolve), "Null library accepted");
    require(!imports.install(fixture.module(), "kernel32.dll", nullptr), "Null resolver accepted");
    require(!imports.install(reinterpret_cast<HMODULE>(fixture.base + 4), "kernel32.dll", resolve),
            "Interior allocation pointer accepted");
    require(!imports.install(fixture.module(), "user32.dll", resolve), "Missing library accepted");
    fixture.text(Fixture::function_offset + 2, "NotHooked");
    fixture.reject();
    fixture.descriptor().OriginalFirstThunk = 0;
    fixture.descriptor().FirstThunk = fixture.size;
    fixture.reject();
    fixture.descriptor() = {};
    fixture.reject();

    Fixture skipped;
    skipped.directory().Size = 3 * sizeof(IMAGE_IMPORT_DESCRIPTOR);
    skipped.descriptor(1) = skipped.descriptor();
    skipped.descriptor().OriginalFirstThunk = 0;
    skipped.descriptor().FirstThunk = skipped.size;
    require(imports.install(skipped.module(), "kernel32.dll", resolve) && imports.active(),
            "Missing original thunk prevented a later valid descriptor");
    imports.remove();
    require(skipped.slots()[0].u1.Function == 0x12345678,
            "Skipped descriptor changed hook restoration");
}

bool first_import = false;
std::string mapped_name;

FARPROC resolve_first(const char* name) {
    if (first_import) {
        return nullptr;
    }
    first_import = true;
    mapped_name = name;
    return &hook;
}

void mapped_image(const char* path) {
    const auto module = LoadLibraryExA(path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    require(module != nullptr, "Cannot map original executable");

    struct Unload {
        HMODULE module;

        ~Unload() {
            FreeLibrary(module);
        }
    } unload{module};

    struct Snapshot {
        BYTE* address;
        std::vector<BYTE> bytes;
    };

    std::vector<Snapshot> snapshots;
    auto* base = reinterpret_cast<BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const auto size = pe->OptionalHeader.SizeOfImage;
    for (std::size_t offset = 0; offset < size;) {
        MEMORY_BASIC_INFORMATION region{};
        require(VirtualQuery(base + offset, &region, sizeof(region)) != 0,
                "Cannot inspect mapped executable pages");
        const auto within =
            static_cast<std::size_t>(base + offset - static_cast<BYTE*>(region.BaseAddress));
        const auto count = std::min(region.RegionSize - within, size - offset);
        require(count != 0, "Empty mapped executable region");
        const auto protection = region.Protect & 0xff;
        if (region.State == MEM_COMMIT && !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (protection == PAGE_READONLY || protection == PAGE_READWRITE ||
             protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
             protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY)) {
            snapshots.push_back({base + offset, {base + offset, base + offset + count}});
        }
        offset += count;
    }
    ImportHooks imports;
    first_import = false;
    require(imports.install(module, "kernel32.dll", resolve_first) && imports.active(),
            "Original executable import layout rejected");
    require(first_import && imports.previous(&hook), "Original mapped import lost");
    imports.remove();
    for (const auto& snapshot : snapshots) {
        require(std::memcmp(snapshot.address, snapshot.bytes.data(), snapshot.bytes.size()) == 0,
                "Mapped executable was not restored exactly");
    }
    std::cout << "Mapped import restored: " << path << " (" << mapped_name << ")\n";
}
}

int main(int argc, char* argv[]) {
    try {
        static_assert(sizeof(ULONG_PTR) == sizeof(DWORD));
        valid_images();
        malformed_images();
        unreadable_images();
        skipped_and_missing();
        for (int index = 1; index < argc; ++index) {
            mapped_image(argv[index]);
        }
        std::cout << "Import bounds, readable pages, rollback and restoration passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
