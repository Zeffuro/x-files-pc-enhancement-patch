#include "state_capture.h"
#include "native_writes.h"
#include "enhancements/game_ui.h"
#include <algorithm>

namespace devtools {
namespace {
thread_local bool enabled = false, reseed = true;
thread_local std::deque<StateHistoryEntry> edits;

auto context(const GameSnapshot& snapshot) {
    return std::array{snapshot.manager, snapshot.state, snapshot.hdb, snapshot.application,
                      snapshot.session};
}

std::wstring action_context(const NativeWrite& write) {
    constexpr std::array operations{L"Assign",   L"Increment", L"Decrement", L"Add",
                                    L"Subtract", L"Multiply",  L"Divide",    L"Remainder"};
    const auto operation =
        write.operation < operations.size() ? operations[write.operation] : L"Unknown";
    return L"Action " + std::to_wstring(write.action_id) + L" / " + operation + L" / Thread " +
           std::to_wstring(write.thread) +
           L". Value observed before and after this native action. Preparatory writes may be "
           L"included.";
}
}

std::wstring set_state_capture(bool requested) {
    if (requested == enabled) {
        return L"";
    }
    enabled = requested;
    reseed = true;
    edits.clear();
    native_writes_reset();
    if (enabled) {
        const auto* image = enhancements::game::executable_image();
        if (image) {
            attach_native_writes(image, enhancements::game::edition());
        }
    }
    native_writes_enable(enabled);
    return L"";
}

bool state_capture_enabled() {
    return enabled;
}

std::wstring state_capture_status() {
    if (!enabled) {
        return L"Capture off. History is retained until the game session changes.";
    }
    const auto status = native_writes_status();
    if (status.attached && !status.context_current) {
        return L"Game context changed. Refresh the snapshot to continue native history capture.";
    }
    std::wstring result = status.attached ? L"Capturing cached native variable actions and "
                                            L"inspector edits. Other changes appear on refresh."
                                          : L"Native action capture unavailable. Capturing "
                                            L"inspector edits and changes on refresh.";
    result +=
        L" Intermediate or uncached writes can be missed. Sampled changes have unknown writers.";
    if (status.dropped || status.skipped) {
        result += L" Native queue dropped " + std::to_wstring(status.dropped) + L", skipped " +
                  std::to_wstring(status.skipped) + L".";
    }
    return result;
}

void collect_state_history(StateHistory& history, const GameSnapshot& snapshot, bool refreshed) {
    if (!enabled) {
        if (refreshed) {
            reset_state_history_baseline(history, snapshot);
        }
        return;
    }
    if (refreshed && (reseed || !history.seeded || history.context != context(snapshot))) {
        edits.clear();
        native_writes_reset();
        reset_state_history_baseline(history, snapshot);
        native_writes_context(snapshot);
        reseed = false;
        return;
    }
    if (reseed) {
        return;
    }
    std::vector<StateHistoryEntry> pending;
    const auto writes = native_writes_drain();
    const auto status = native_writes_status();
    if (status.attached && !status.context_current) {
        clear_state_history(history);
        history.seeded = false;
        history.previous.clear();
        edits.clear();
        reseed = true;
        if (refreshed) {
            reset_state_history_baseline(history, snapshot);
            native_writes_context(snapshot);
            reseed = false;
        }
        return;
    }
    for (const auto& write : writes) {
        StateHistoryEntry entry;
        entry.ticks = write.tick;
        entry.key = write.key;
        entry.address = write.address;
        const auto found =
            std::find_if(history.previous.begin(), history.previous.end(), [&](const auto& row) {
                return row.key == write.key && row.address == write.address;
            });
        if (found != history.previous.end()) {
            entry.name = found->name;
        }
        entry.before = write.before;
        entry.after = write.after;
        entry.type_flags = write.type_flags;
        if (write.caller_rva) {
            entry.caller_rva = write.caller_rva;
        }
        entry.context = action_context(write);
        entry.origin = StateChangeOrigin::native_action;
        pending.push_back(std::move(entry));
    }
    while (!edits.empty()) {
        pending.push_back(std::move(edits.front()));
        edits.pop_front();
    }
    std::stable_sort(pending.begin(), pending.end(),
                     [](const auto& left, const auto& right) { return left.ticks < right.ticks; });
    for (auto& entry : pending) {
        append_state_history(history, std::move(entry));
    }
    if (refreshed) {
        observe_state_history(history, snapshot, GetTickCount64());
        native_writes_context(snapshot);
    }
}

void release_state_capture() {
    enabled = false;
    reseed = true;
    edits.clear();
    release_native_writes();
}

void record_inspector_write(const GameSnapshot& snapshot, const StateVariable& variable,
                            std::int32_t after) {
    if (!enabled || reseed || !snapshot.session || !variable.value ||
        variable.value->raw_value == after) {
        return;
    }
    StateHistoryEntry entry;
    entry.ticks = GetTickCount64();
    entry.key = variable.key;
    entry.address = variable.address;
    entry.name = variable.name;
    entry.before = variable.value->raw_value;
    entry.after = after;
    entry.type_flags = variable.value->type_flags;
    entry.origin = StateChangeOrigin::inspector;
    entry.context = L"Applied through the guarded native variable editor.";
    if (edits.size() == StateHistory::capacity) {
        edits.pop_front();
    }
    edits.push_back(std::move(entry));
}
}
