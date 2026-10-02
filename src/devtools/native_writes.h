#pragma once
#include "game_state.h"
#include <cstdint>
#include <vector>

namespace devtools {
struct NativeWrite {
    DatabaseObjectKey key;
    std::uintptr_t address = 0;
    std::uint64_t sequence = 0, tick = 0;
    std::uint32_t thread = 0, caller_rva = 0, action_id = 0;
    std::int32_t before = 0, after = 0;
    std::uint8_t type_flags = 0, operation = 0;
};

struct NativeWriteStatus {
    bool attached = false, enabled = false;
    std::uint64_t dropped = 0, skipped = 0;
    std::size_t targets = 0;
    bool context_current = false;
};

bool attach_native_writes(const std::byte* image, const native_game::Profile& profile);
void native_writes_enable(bool enabled);
void native_writes_context(const GameSnapshot& snapshot);
std::vector<NativeWrite> native_writes_drain();
NativeWriteStatus native_writes_status();
void native_writes_reset();
// Installed callbacks and their module remain resident. Release only stops capture.
void release_native_writes();
}
