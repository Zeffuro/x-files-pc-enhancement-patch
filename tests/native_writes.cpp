#include "devtools/native_writes.h"
#include "game/layouts/database/variable.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
std::byte* image = nullptr;
std::uintptr_t received[3]{};
unsigned calls = 0;
DWORD incoming_error = 0;
bool reset_during_call = false, nested_call = false;
using Callback = std::uint32_t(__stdcall*)(std::uintptr_t, std::uintptr_t, std::uintptr_t);
Callback callback = nullptr;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class T> void put(std::size_t offset, const T& value) {
    std::memcpy(image + offset, &value, sizeof(value));
}

std::uint32_t address(std::size_t offset) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image + offset));
}

std::uint32_t __stdcall action(std::uintptr_t object, std::uintptr_t context,
                               std::uintptr_t output) {
    incoming_error = GetLastError();
    received[0] = object;
    received[1] = context;
    received[2] = output;
    ++calls;
    auto* variable = reinterpret_cast<native_game::Variable*>(image + 0x16000);
    ++variable->raw_value;
    if (nested_call) {
        nested_call = false;
        callback(object, context, output);
    }
    if (reset_during_call) {
        devtools::native_writes_reset();
    }
    SetLastError(1234);
    return 0x89abcdef;
}

void verify(unsigned edition) {
    const std::array profiles{&native_game::profile_cd_10012, &native_game::profile_cd_10019,
                              &native_game::profile_cd_10020, &native_game::profile_dvd_20000};
    constexpr std::array<std::uint32_t, 4> slots{0x253464, 0x255474, 0x256474, 0x25dbd4};
    constexpr std::array<std::uint32_t, 4> methods{0x33540, 0x32f90, 0x33090, 0x33060};
    const auto& profile = *profiles.at(edition);
    image = static_cast<std::byte*>(
        VirtualAlloc(nullptr, 0x300000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    require(image != nullptr, "Cannot allocate fixture");
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = 0x80;
    put(0, dos);
    IMAGE_NT_HEADERS32 pe{};
    pe.Signature = IMAGE_NT_SIGNATURE;
    pe.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    pe.FileHeader.NumberOfSections = 1;
    pe.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    pe.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    pe.OptionalHeader.SizeOfImage = 0x300000;
    put(0x80, pe);
    IMAGE_SECTION_HEADER section{};
    section.VirtualAddress = 0x1000;
    section.Misc.VirtualSize = 0x2ff000;
    section.Characteristics = IMAGE_SCN_MEM_EXECUTE;
    put(0x80 + sizeof(pe), section);
    const auto slot = slots[edition], method = methods[edition];
    const std::array<std::uint8_t, 14> cd{0x53, 0x56, 0x8b, 0x5c, 0x24, 0x0c, 0x57,
                                          0x55, 0x8b, 0xab, 0x3c, 0x01, 0,    0};
    const std::array<std::uint8_t, 14> dvd{0x53, 0x56, 0x57, 0x55, 0x8b, 0x7c, 0x24,
                                           0x14, 0x8b, 0xb7, 0x3c, 0x01, 0,    0};
    put(method, edition == 3 ? dvd : cd);
    put(method + 14, std::array<std::uint8_t, 5>{0x5d, 0x5f, 0x5e, 0x5b, 0xe9});
    put(method + 19, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&action) -
                                                address(method + 23)));
    put(slot, address(method));
    put(profile.database_manager, address(0x10000));
    put(profile.application, address(0x13000));
    put(0x13000 + 0xe4, address(0x14000));
    put(0x10000 + 0xd8 + 0x18, address(0x11000));
    put(0x10000 + 4 + 0x18, address(0x12000));
    put(0x20000 + 0x13c, address(0x21000));
    put(0x21000 + 4, std::uint32_t{0xabcdef12});
    put(0x21000 + 0x60, address(0x22000));
    put(0x21000 + 0x64, std::uint8_t{0});
    put(0x22000, std::uint32_t{0xf0123456});
    put(0x22000 + 10, std::uint8_t{1});
    native_game::Variable variable{};
    variable.vtable = address(0x25000);
    variable.id = 0xf0123456;
    variable.type_flags = 0x81;
    variable.raw_value = 7;
    put(0x16000, variable);
    devtools::GameSnapshot snapshot;
    snapshot.manager = address(0x10000);
    snapshot.state = address(0x10000 + 0xd8);
    snapshot.application = address(0x13000);
    snapshot.session = address(0x14000);
    devtools::StateVariable row;
    row.key = {0x53, variable.id, true};
    row.address = address(0x16000);
    row.value = {variable.raw_value, variable.type_flags};
    row.bytes.resize(sizeof(variable));
    std::memcpy(row.bytes.data(), &variable, sizeof(variable));
    snapshot.variables.push_back(row);
    require(!devtools::attach_native_writes(nullptr, profile), "Null image accepted");
    pe.OptionalHeader.SizeOfImage = 0x1000;
    put(0x80, pe);
    require(!devtools::attach_native_writes(image, profile), "Truncated PE bounds accepted");
    pe.OptionalHeader.SizeOfImage = 0x300000;
    put(0x80, pe);
    image[method] = std::byte{0};
    require(!devtools::attach_native_writes(image, profile), "Wrong code accepted");
    put(method, edition == 3 ? dvd : cd);
    put(slot, address(method + 1));
    require(!devtools::attach_native_writes(image, profile), "Occupied slot replaced");
    put(slot, address(method));
    auto unknown = profile;
    unknown.variable_set_value = 0;
    require(!devtools::attach_native_writes(image, unknown), "Unknown build accepted");
    unknown = profile;
    unknown.native_action_write_slot += 4;
    require(!devtools::attach_native_writes(image, unknown), "Mismatched profile tuple accepted");
    require(devtools::attach_native_writes(image, profile), "Verified attachment rejected");
    require(devtools::attach_native_writes(image, profile), "Attachment is not idempotent");
    require(!devtools::attach_native_writes(image, unknown),
            "Attached hook accepted another tuple");
    std::memcpy(&callback, image + slot, sizeof(callback));
    std::vector<std::uint8_t> caller;
    const auto append = [&](std::uint32_t value) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        caller.insert(caller.end(), bytes, bytes + sizeof(value));
    };
    for (const auto value : {std::uint32_t{303}, std::uint32_t{202}, address(0x20000)}) {
        caller.push_back(0x68);
        append(value);
    }
    caller.insert(caller.end(), {0xff, 0x15});
    append(address(slot));
    caller.push_back(0xc3);
    std::memcpy(image + 0x2e0000, caller.data(), caller.size());
    FlushInstructionCache(GetCurrentProcess(), image, 0x300000);
    const auto invoke = reinterpret_cast<std::uint32_t(__cdecl*)()>(image + 0x2e0000);
    require(invoke() == 0x89abcdef, "Disabled hook changed native EAX");
    require(calls == 1 && devtools::native_writes_drain().empty(), "Off-default gate failed");
    devtools::native_writes_context(snapshot);
    devtools::native_writes_enable(true);
    SetLastError(5678);
    require(invoke() == 0x89abcdef, "Enabled hook changed native EAX");
    require(incoming_error == 5678 && GetLastError() == 1234, "LastError contract changed");
    auto records = devtools::native_writes_drain();
    require(calls == 2 && received[0] == address(0x20000) && received[1] == 202 &&
                received[2] == 303,
            "Original argument forwarding changed");
    require(records.size() == 1 && records[0].before == 8 && records[0].after == 9 &&
                records[0].key == row.key && records[0].address == row.address &&
                records[0].caller_rva == 0x2e0015 && records[0].action_id == 0xabcdef12 &&
                records[0].operation == 1 && records[0].type_flags == 0x81 &&
                records[0].thread == GetCurrentThreadId(),
            "Native action record has wrong values or provenance");
    put(0x22000 + 12, variable.id);
    put(0x22000 + 12 + 10, std::uint8_t{7});
    put(0x21000 + 0x64, std::uint8_t{0x80});
    invoke();
    records = devtools::native_writes_drain();
    require(records.size() == 1 && records[0].operation == 7, "Alternate operands not selected");
    put(0x21000 + 0x64, std::uint8_t{1});
    invoke();
    require(devtools::native_writes_drain().empty(), "Nonvariable action attributed");
    put(0x21000 + 0x64, std::uint8_t{0});
    put(0x22000 + 8, std::uint8_t{1});
    invoke();
    require(devtools::native_writes_drain().empty(), "Nonvariable target attributed");
    put(0x22000 + 8, std::uint8_t{0});
    put(0x21000 + 0x60, std::uint32_t{0});
    invoke();
    require(devtools::native_writes_drain().empty(), "Cold descriptor attributed");
    put(0x21000 + 0x60, address(0x22000));
    put(profile.database_manager, std::uint32_t{0});
    invoke();
    require(devtools::native_writes_drain().empty(), "Stale manager attributed");
    put(profile.database_manager, address(0x10000));
    devtools::native_writes_context(snapshot);
    put(0x10000 + 0xd8 + 0x18, std::uint32_t{0});
    invoke();
    require(devtools::native_writes_drain().empty(), "Replaced state root attributed");
    put(0x10000 + 0xd8 + 0x18, address(0x11000));
    devtools::native_writes_context(snapshot);
    invoke();
    put(0x13000 + 0xe4, address(0x14004));
    require(devtools::native_writes_drain().empty() &&
                !devtools::native_writes_status().context_current,
            "Pending event survived session replacement");
    put(0x13000 + 0xe4, address(0x14000));
    devtools::native_writes_context(snapshot);
    auto duplicate = snapshot;
    duplicate.variables.push_back(row);
    devtools::native_writes_context(duplicate);
    invoke();
    require(devtools::native_writes_drain().empty(), "Duplicate identity attributed");
    devtools::native_writes_context(snapshot);
    std::thread other([&] { invoke(); });
    other.join();
    require(devtools::native_writes_drain().empty(), "Foreign thread attributed");
    nested_call = true;
    invoke();
    records = devtools::native_writes_drain();
    require(records.size() == 2 && records[0].after == records[1].after &&
                records[1].after == records[1].before + 2,
            "Nested native calls lost original behavior");
    reset_during_call = true;
    invoke();
    require(devtools::native_writes_drain().empty(), "Reset retained an in-flight record");
    reset_during_call = false;
    devtools::native_writes_context(snapshot);
    for (unsigned index = 0; index < 1100; ++index) {
        invoke();
    }
    records = devtools::native_writes_drain();
    require(records.size() == 1024 && devtools::native_writes_status().dropped == 76 &&
                records.back().sequence - records.front().sequence == 1023,
            "Bounded ring eviction lost ordering or counters");
    devtools::native_writes_enable(false);
    invoke();
    require(devtools::native_writes_drain().empty(), "Disabled capture retained a write");
    devtools::native_writes_enable(true);
    devtools::release_native_writes();
    const auto before = calls;
    invoke();
    require(calls == before + 1 && devtools::native_writes_drain().empty() &&
                !devtools::native_writes_status().enabled &&
                devtools::native_writes_status().attached,
            "Released callback lost forwarding or continued recording");
}
}

int main(int argc, char** argv) {
    try {
        verify(argc > 1 ? static_cast<unsigned>(std::stoul(argv[1])) : 0);
        std::cout
            << "Atomic native action capture, guards, forwarding and bounded history passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
