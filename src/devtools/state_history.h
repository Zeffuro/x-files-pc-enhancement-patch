#pragma once
#include "game_state.h"
#include <array>
#include <deque>

namespace devtools {
enum class StateChangeOrigin { native_action, inspector, snapshot };

struct StateHistoryEntry {
    std::uint64_t sequence = 0, ticks = 0;
    std::optional<DatabaseObjectKey> key;
    std::uintptr_t address = 0;
    std::wstring name;
    std::int32_t before = 0, after = 0;
    std::uint8_t type_flags = 0;
    std::optional<std::uint32_t> caller_rva;
    std::wstring context;
    StateChangeOrigin origin = StateChangeOrigin::snapshot;
};

struct StateHistory {
    static constexpr std::size_t capacity = 512;
    std::deque<StateHistoryEntry> entries;
    std::size_t dropped = 0;
    std::uint64_t revision = 0, next_sequence = 1, observed_sequence = 0;
    std::array<std::uintptr_t, 5> context{};
    std::vector<StateVariable> previous;
    bool seeded = false, baseline_pending = false;
};

void clear_state_history(StateHistory& history);
void append_state_history(StateHistory& history, StateHistoryEntry entry);
void reset_state_history_baseline(StateHistory& history, const GameSnapshot& snapshot);
bool observe_state_history(StateHistory& history, const GameSnapshot& snapshot,
                           std::uint64_t ticks);
std::wstring state_history_text(const StateHistoryEntry& entry);
std::wstring state_history_origin(const StateHistoryEntry& entry);
std::wstring state_history_caller(const StateHistoryEntry& entry);
}
