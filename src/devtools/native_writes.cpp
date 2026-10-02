#include "native_writes.h"
#include "database/memory.h"
#include "game/layouts/database/variable.h"
#include "game/layouts/ui.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace devtools {
namespace {
static_assert(sizeof(void*) == 4);
using Action = std::uint32_t(__stdcall*)(std::uintptr_t, std::uintptr_t, std::uintptr_t);

struct Mapping {
    std::uint32_t setter, slot, callback;

    constexpr explicit Mapping(const native_game::Profile& profile)
        : setter(profile.variable_set_value), slot(profile.native_action_write_slot),
          callback(profile.native_action_write_callback) {}
};

constexpr std::array mappings{
    Mapping{native_game::profile_cd_10012}, Mapping{native_game::profile_cd_10019},
    Mapping{native_game::profile_cd_10020}, Mapping{native_game::profile_dvd_20000}};

struct Target {
    std::uint32_t id = 0, vtable = 0;
    std::uintptr_t address = 0;
};

struct Observation {
    Target target;
    native_game::Variable before{};
    std::uint64_t generation = 0;
    std::uint32_t action_id = 0;
    std::uint8_t operation = 0;
};

constexpr std::size_t capacity = 1024, target_capacity = 16384;
SRWLOCK lock = SRWLOCK_INIT;
std::array<Target, target_capacity> targets{};
std::array<NativeWrite, capacity> records{};
std::size_t target_count = 0, record_count = 0, record_start = 0;
std::uint64_t generation = 1, sequence = 0;
std::uintptr_t manager = 0, state = 0;
std::uintptr_t application = 0, session = 0;
std::uint32_t state_root = 0, hdb_root = 0;
DWORD owner_thread = 0;
std::atomic<bool> enabled = false, attached = false;
std::atomic<std::uint64_t> dropped = 0, skipped = 0;
const std::byte* image = nullptr;
std::uint32_t manager_rva = 0;
std::uint32_t application_rva = 0;

struct CodeRange {
    std::uint32_t first = 0, last = 0;
};

std::array<CodeRange, 96> executable_ranges{};
std::size_t executable_range_count = 0;
Action original = nullptr;

template <class T> bool read(std::uintptr_t address, T& value) {
    return database_copy(address, &value, sizeof(value));
}

bool current_context() {
    std::uint32_t current = 0, current_state = 0, current_hdb = 0, app = 0, active = 0;
    return manager && application && session && state == manager + 0xd8 &&
           owner_thread == GetCurrentThreadId() &&
           read(reinterpret_cast<std::uintptr_t>(image) + manager_rva, current) &&
           current == manager && read(state + 0x18, current_state) && current_state == state_root &&
           read(manager + 4 + 0x18, current_hdb) && current_hdb == hdb_root &&
           read(reinterpret_cast<std::uintptr_t>(image) + application_rva, app) &&
           app == application &&
           read(application + offsetof(native_game::Application, state), active) &&
           active == session;
}

bool executable_rva(std::uint32_t rva) {
    return std::any_of(executable_ranges.begin(),
                       executable_ranges.begin() + executable_range_count,
                       [&](const auto& range) { return rva >= range.first && rva < range.last; });
}

bool image_ranges(std::uintptr_t base, const Mapping& mapping,
                  const native_game::Profile& profile) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 pe{};
    if (!read(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 64 ||
        dos.e_lfanew > 0x100000 || !read(base + dos.e_lfanew, pe) ||
        pe.Signature != IMAGE_NT_SIGNATURE || pe.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER32) ||
        !pe.FileHeader.NumberOfSections ||
        pe.FileHeader.NumberOfSections > executable_ranges.size()) {
        return false;
    }
    const auto size = pe.OptionalHeader.SizeOfImage;
    if (size < 4096 || base > UINT32_MAX - size || mapping.slot > size - 4 ||
        mapping.callback > size - 12 || profile.application > size - 4 ||
        profile.database_manager > size - 4) {
        return false;
    }
    executable_range_count = 0;
    for (unsigned index = 0; index < pe.FileHeader.NumberOfSections; ++index) {
        IMAGE_SECTION_HEADER section{};
        if (!read(base + dos.e_lfanew + sizeof(pe) + index * sizeof(section), section) ||
            section.VirtualAddress >= size ||
            section.Misc.VirtualSize > size - section.VirtualAddress) {
            return false;
        }
        if (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            executable_ranges[executable_range_count++] = {
                section.VirtualAddress, section.VirtualAddress + section.Misc.VirtualSize};
        }
    }
    return executable_rva(mapping.callback) && executable_rva(mapping.callback + 11);
}

Observation begin(std::uintptr_t runtime_action) {
    Observation result;
    if (!enabled.load(std::memory_order_acquire)) {
        return result;
    }
    std::uint32_t action = 0, descriptor = 0, id = 0;
    std::uint8_t flags = 0;
    std::array<std::uint8_t, 12> operands{};
    if (!read(runtime_action + 0x13c, action) || !action || !read(action + 4, id) ||
        !read(action + 0x64, flags) || (flags & 0x7f) != 0 || !read(action + 0x60, descriptor) ||
        !descriptor || !read(std::uintptr_t(descriptor) + (flags & 0x80 ? 12 : 0), operands) ||
        operands[8] != 0 || operands[10] > 7) {
        ++skipped;
        return result;
    }
    std::uint32_t target_id = 0;
    std::memcpy(&target_id, operands.data(), sizeof(target_id));
    if (!TryAcquireSRWLockShared(&lock)) {
        ++dropped;
        return result;
    }
    const auto found = std::lower_bound(targets.begin(), targets.begin() + target_count, target_id,
                                        [](const Target& a, auto b) { return a.id < b; });
    if (enabled.load(std::memory_order_relaxed) && current_context() &&
        found != targets.begin() + target_count && found->id == target_id && found->address) {
        result.target = *found;
        result.generation = generation;
        result.action_id = id;
        result.operation = operands[10];
    }
    ReleaseSRWLockShared(&lock);
    if (!result.target.address || !read(result.target.address, result.before) ||
        result.before.id != result.target.id || result.before.vtable != result.target.vtable ||
        (result.before.type_flags & 0x7f) > 2) {
        result = {};
        ++skipped;
    }
    return result;
}

void finish(const Observation& observation, std::uintptr_t caller) {
    if (!observation.target.address || !enabled.load(std::memory_order_acquire)) {
        return;
    }
    native_game::Variable after{};
    if (!read(observation.target.address, after) || after.id != observation.before.id ||
        after.vtable != observation.before.vtable ||
        after.type_flags != observation.before.type_flags) {
        ++skipped;
        return;
    }
    if (!TryAcquireSRWLockExclusive(&lock)) {
        ++dropped;
        return;
    }
    if (enabled.load(std::memory_order_relaxed) && observation.generation == generation &&
        current_context()) {
        const auto base = reinterpret_cast<std::uintptr_t>(image);
        NativeWrite record;
        record.key = {0x53, after.id, true};
        record.address = observation.target.address;
        record.sequence = ++sequence;
        record.tick = GetTickCount64();
        record.thread = GetCurrentThreadId();
        record.caller_rva =
            caller >= base && executable_rva(static_cast<std::uint32_t>(caller - base))
                ? static_cast<std::uint32_t>(caller - base)
                : 0;
        record.action_id = observation.action_id;
        record.operation = observation.operation;
        record.before = observation.before.raw_value;
        record.after = after.raw_value;
        record.type_flags = after.type_flags;
        if (record_count == capacity) {
            record_start = (record_start + 1) % capacity;
            --record_count;
            ++dropped;
        }
        records[(record_start + record_count++) % capacity] = record;
    } else {
        ++skipped;
    }
    ReleaseSRWLockExclusive(&lock);
}

std::uint32_t __stdcall observe_action(std::uintptr_t object, std::uintptr_t context,
                                       std::uintptr_t output) {
    const auto incoming_error = GetLastError();
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto observation = begin(object);
    SetLastError(incoming_error);
    const auto result = original(object, context, output);
    const auto outgoing_error = GetLastError();
    finish(observation, caller);
    SetLastError(outgoing_error);
    return result;
}

void clear() {
    ++generation;
    record_count = record_start = target_count = 0;
    manager = state = 0;
    application = session = 0;
    state_root = hdb_root = 0;
    owner_thread = 0;
    dropped = skipped = 0;
}
}

bool attach_native_writes(const std::byte* executable, const native_game::Profile& profile) {
    AcquireSRWLockExclusive(&lock);
    const auto found = std::find_if(mappings.begin(), mappings.end(), [&](const auto& mapping) {
        return mapping.setter == profile.variable_set_value &&
               mapping.slot == profile.native_action_write_slot &&
               mapping.callback == profile.native_action_write_callback;
    });
    if (attached.load(std::memory_order_relaxed)) {
        const bool same = found != mappings.end() && executable == image &&
                          profile.database_manager == manager_rva &&
                          profile.application == application_rva &&
                          reinterpret_cast<std::uintptr_t>(original) ==
                              reinterpret_cast<std::uintptr_t>(image) + found->callback;
        ReleaseSRWLockExclusive(&lock);
        return same;
    }
    constexpr std::array<std::uint8_t, 12> cd_prefix{0x53, 0x56, 0x8b, 0x5c, 0x24, 0x0c,
                                                     0x57, 0x55, 0x8b, 0xab, 0x3c, 0x01};
    constexpr std::array<std::uint8_t, 12> dvd_prefix{0x53, 0x56, 0x57, 0x55, 0x8b, 0x7c,
                                                      0x24, 0x14, 0x8b, 0xb7, 0x3c, 0x01};
    std::array<std::uint8_t, 12> code{};
    std::uintptr_t current = 0;
    const auto base = reinterpret_cast<std::uintptr_t>(executable);
    if (!executable || found == mappings.end() || !image_ranges(base, *found, profile) ||
        (base + found->slot) % sizeof(void*) || !read(base + found->callback, code) ||
        code != (found->setter == 0x1e0780 ? dvd_prefix : cd_prefix) ||
        !read(base + found->slot, current) || current != base + found->callback) {
        ReleaseSRWLockExclusive(&lock);
        return false;
    }
    HMODULE module = nullptr;
    DWORD protection = 0;
    auto** slot = reinterpret_cast<void**>(base + found->slot);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<const wchar_t*>(&observe_action), &module) ||
        !VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) {
        ReleaseSRWLockExclusive(&lock);
        return false;
    }
    image = executable;
    manager_rva = profile.database_manager;
    application_rva = profile.application;
    original = reinterpret_cast<Action>(base + found->callback);
    // An aligned pointer exchange keeps original code and in-flight native frames intact.
    const auto previous = InterlockedCompareExchangePointer(
        slot, reinterpret_cast<void*>(&observe_action), reinterpret_cast<void*>(original));
    DWORD unused = 0;
    VirtualProtect(slot, sizeof(void*), protection, &unused);
    const bool success = previous == reinterpret_cast<void*>(original);
    attached.store(success, std::memory_order_release);
    ReleaseSRWLockExclusive(&lock);
    return success;
}

void native_writes_enable(bool active) {
    AcquireSRWLockExclusive(&lock);
    ++generation;
    enabled.store(active && attached.load(std::memory_order_relaxed), std::memory_order_release);
    ReleaseSRWLockExclusive(&lock);
}

void native_writes_context(const GameSnapshot& snapshot) {
    std::vector<Target> next;
    next.reserve(std::min(snapshot.variables.size(), target_capacity));
    for (const auto& row : snapshot.variables) {
        if (row.key.class_id != 0x53 || !row.key.state_database || !row.value || !row.address ||
            row.bytes.size() < sizeof(native_game::Variable)) {
            continue;
        }
        native_game::Variable variable{};
        std::memcpy(&variable, row.bytes.data(), sizeof(variable));
        if (variable.id == row.key.id && (variable.type_flags & 0x7f) <= 2) {
            next.push_back({variable.id, variable.vtable, row.address});
        }
    }
    std::sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (std::size_t index = 0; index < next.size();) {
        auto end = index + 1;
        while (end < next.size() && next[end].id == next[index].id) {
            ++end;
        }
        if (end != index + 1) {
            for (auto duplicate = index; duplicate < end; ++duplicate) {
                next[duplicate].address = 0;
            }
        }
        index = end;
    }
    AcquireSRWLockExclusive(&lock);
    if (manager != snapshot.manager || state != snapshot.state ||
        application != snapshot.application || session != snapshot.session || !current_context()) {
        skipped += record_count;
        record_count = record_start = 0;
    }
    ++generation;
    manager = snapshot.manager;
    state = snapshot.state;
    application = snapshot.application;
    session = snapshot.session;
    if (!read(state + 0x18, state_root) || !read(manager + 4 + 0x18, hdb_root)) {
        manager = state = 0;
    }
    owner_thread = GetCurrentThreadId();
    target_count = std::min(next.size(), targets.size());
    std::copy_n(next.begin(), target_count, targets.begin());
    if (next.size() > target_count) {
        skipped += next.size() - target_count;
    }
    ReleaseSRWLockExclusive(&lock);
}

std::vector<NativeWrite> native_writes_drain() {
    std::vector<NativeWrite> result;
    result.reserve(capacity);
    AcquireSRWLockExclusive(&lock);
    if (!current_context()) {
        skipped += record_count;
        record_count = record_start = target_count = 0;
        ++generation;
    }
    for (std::size_t index = 0; index < record_count; ++index) {
        result.push_back(records[(record_start + index) % capacity]);
    }
    record_count = record_start = 0;
    ReleaseSRWLockExclusive(&lock);
    return result;
}

NativeWriteStatus native_writes_status() {
    AcquireSRWLockShared(&lock);
    const NativeWriteStatus result{attached.load(), enabled.load(), dropped.load(),
                                   skipped.load(),  target_count,   current_context()};
    ReleaseSRWLockShared(&lock);
    return result;
}

void native_writes_reset() {
    AcquireSRWLockExclusive(&lock);
    clear();
    ReleaseSRWLockExclusive(&lock);
}

void release_native_writes() {
    enabled.store(false, std::memory_order_release);
    native_writes_reset();
}
}
