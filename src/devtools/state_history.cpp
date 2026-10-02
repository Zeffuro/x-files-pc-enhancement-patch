#include "state_history.h"
#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <tuple>

namespace devtools {
namespace {
auto context(const GameSnapshot& snapshot) {
    return std::array{snapshot.manager, snapshot.state, snapshot.hdb, snapshot.application,
                      snapshot.session};
}

using Key = std::tuple<bool, std::uint32_t, std::uint32_t>;

Key key(const StateVariable& variable) {
    return {variable.key.state_database, variable.key.class_id, variable.key.id};
}

auto unique_rows(const std::vector<StateVariable>& rows) {
    std::map<Key, const StateVariable*> result;
    for (const auto& row : rows) {
        const auto [item, inserted] = result.emplace(key(row), &row);
        if (!inserted) {
            item->second = nullptr;
        }
    }
    return result;
}

bool accounted(const StateHistory& history, const StateVariable& before,
               const StateVariable& after) {
    auto value = before.value->raw_value;
    bool observed = false;
    for (const auto& event : history.entries) {
        if (event.sequence <= history.observed_sequence || !event.key || *event.key != before.key ||
            event.address != before.address || event.origin == StateChangeOrigin::snapshot) {
            continue;
        }
        if (event.before != value || event.type_flags != after.value->type_flags) {
            return false;
        }
        value = event.after;
        observed = true;
    }
    return observed && value == after.value->raw_value;
}
}

void clear_state_history(StateHistory& history) {
    history.entries.clear();
    history.dropped = 0;
    history.baseline_pending = true;
    ++history.revision;
}

void append_state_history(StateHistory& history, StateHistoryEntry entry) {
    if (history.entries.size() == StateHistory::capacity) {
        history.entries.pop_front();
        ++history.dropped;
    }
    entry.sequence = history.next_sequence++;
    history.entries.push_back(std::move(entry));
    ++history.revision;
}

void reset_state_history_baseline(StateHistory& history, const GameSnapshot& snapshot) {
    const auto next = context(snapshot);
    if (history.seeded && history.context != next) {
        clear_state_history(history);
    }
    history.context = next;
    history.previous = snapshot.variables;
    history.seeded = true;
    history.baseline_pending = false;
    history.observed_sequence = history.next_sequence - 1;
}

bool observe_state_history(StateHistory& history, const GameSnapshot& snapshot,
                           std::uint64_t ticks) {
    const bool reset =
        !history.seeded || history.baseline_pending || history.context != context(snapshot);
    if (reset) {
        reset_state_history_baseline(history, snapshot);
        return true;
    }
    const auto previous = unique_rows(history.previous);
    const auto current = unique_rows(snapshot.variables);
    for (const auto& [identity, row] : current) {
        const auto found = previous.find(identity);
        const auto* old = found != previous.end() ? found->second : nullptr;
        if (!row || !old || !old->value || !row->value || !row->address ||
            old->address != row->address || old->name != row->name ||
            old->value->type_flags != row->value->type_flags ||
            old->value->raw_value == row->value->raw_value || accounted(history, *old, *row)) {
            continue;
        }
        StateHistoryEntry entry;
        entry.ticks = ticks;
        entry.key = row->key;
        entry.address = row->address;
        entry.name = row->name;
        entry.before = old->value->raw_value;
        entry.after = row->value->raw_value;
        entry.type_flags = row->value->type_flags;
        entry.context = L"Observed between snapshots. Intermediate writes may be missing.";
        append_state_history(history, std::move(entry));
    }
    reset_state_history_baseline(history, snapshot);
    return false;
}

std::wstring state_history_origin(const StateHistoryEntry& entry) {
    switch (entry.origin) {
        case StateChangeOrigin::native_action:
            return L"Native action";
        case StateChangeOrigin::inspector:
            return L"Inspector edit";
        default:
            return L"Sampled change";
    }
}

std::wstring state_history_caller(const StateHistoryEntry& entry) {
    if (!entry.caller_rva) {
        return entry.origin == StateChangeOrigin::inspector ? L"Inspector" : L"Unknown";
    }
    std::wostringstream result;
    result << L"Game +0x" << std::hex << std::setw(8) << std::setfill(L'0') << *entry.caller_rva;
    return result.str();
}

std::wstring state_history_text(const StateHistoryEntry& entry) {
    std::wstring result = std::to_wstring(entry.sequence) + L"\t" + std::to_wstring(entry.ticks) +
                          L" ms\t" + entry.name;
    if (entry.key) {
        result += entry.key->state_database ? L"\tState " : L"\tHDB ";
        result += std::to_wstring(entry.key->class_id) + L":" + std::to_wstring(entry.key->id);
    } else {
        result += L"\tUnmapped object";
    }
    result += L"\t" + std::to_wstring(entry.before) + L" -> " + std::to_wstring(entry.after) +
              L"\t" + state_history_origin(entry) + L"\t" + state_history_caller(entry);
    if (!entry.context.empty()) {
        result += L"\t" + entry.context;
    }
    return result;
}
}
