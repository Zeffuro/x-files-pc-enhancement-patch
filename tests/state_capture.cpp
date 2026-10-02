#include "devtools/state_capture.h"
#include "devtools/native_writes.h"
#include "enhancements/game_ui.h"
#include <iostream>
#include <stdexcept>

namespace {
std::vector<devtools::NativeWrite> queue;
devtools::NativeWriteStatus status{false, false, 0, 0, 0, true};
devtools::GameSnapshot armed;
unsigned drains = 0, resets = 0, attaches = 0, releases = 0;
bool attach_ok = true;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

devtools::GameSnapshot snapshot() {
    devtools::GameSnapshot result;
    result.manager = 1;
    result.state = 2;
    result.hdb = 3;
    result.application = 4;
    result.session = 5;
    result.variables = {
        {{0x53, 42, true}, L"Location", L"", devtools::DatabaseVariable{65, 0x80}, 100}};
    return result;
}

devtools::NativeWrite event(std::uint64_t tick, int before, int after) {
    devtools::NativeWrite value;
    value.key = {0x53, 42, true};
    value.address = 100;
    value.tick = tick;
    value.before = before;
    value.after = after;
    value.type_flags = 0x80;
    value.action_id = 7;
    value.caller_rva = 0x12345;
    return value;
}
}

namespace enhancements::game {
std::byte* executable_image() {
    return reinterpret_cast<std::byte*>(1);
}

const Edition& edition() {
    return dvd;
}
}

namespace devtools {
bool attach_native_writes(const std::byte*, const native_game::Profile&) {
    ++attaches;
    status.attached = attach_ok;
    return attach_ok;
}

void native_writes_enable(bool value) {
    status.enabled = value;
}

void native_writes_context(const GameSnapshot& value) {
    armed = value;
    status.context_current = true;
}

std::vector<NativeWrite> native_writes_drain() {
    ++drains;
    auto result = std::move(queue);
    queue.clear();
    return result;
}

NativeWriteStatus native_writes_status() {
    return status;
}

void native_writes_reset() {
    ++resets;
    queue.clear();
}

void release_native_writes() {
    ++releases;
    status.enabled = false;
    queue.clear();
}
}

int main() {
    try {
        devtools::StateHistory history;
        auto held = snapshot();
        devtools::collect_state_history(history, held, true);
        require(!devtools::state_capture_enabled() && history.entries.empty() && drains == 0,
                "Capture default or disabled refresh drained native writes");
        require(devtools::set_state_capture(true).empty() && attaches == 1 && status.enabled,
                "Capture enable did not arm native observation");
        queue.push_back(event(1, 1, 2));
        devtools::collect_state_history(history, held);
        require(drains == 0 && queue.size() == 1,
                "Unseeded capture consumed writes before fresh baseline");
        devtools::collect_state_history(history, held, true);
        require(queue.empty() && history.entries.empty() && armed.session == held.session,
                "Initial fresh baseline retained pre-capture events");

        devtools::record_inspector_write(held, held.variables.front(), 66);
        const auto now = GetTickCount64();
        queue.push_back(event(now + 100, 66, 67));
        queue.push_back(event(0, 64, 65));
        devtools::collect_state_history(history, held);
        require(history.entries.size() == 3 && history.entries[0].ticks == 0 &&
                    history.entries[1].origin == devtools::StateChangeOrigin::inspector &&
                    history.entries[2].ticks == now + 100,
                "Native and inspector drains were not merged in timestamp order");
        require(held.variables.front().value->raw_value == 65 &&
                    history.entries[2].name == L"Location" &&
                    history.entries[2].context.find(L"Action 7") != std::wstring::npos,
                "Drain modified held snapshot or omitted owned action/name evidence");

        devtools::set_state_capture(false);
        const auto retained = history.entries.size();
        auto fresh = held;
        fresh.variables.front().value->raw_value = 80;
        devtools::record_inspector_write(fresh, fresh.variables.front(), 81);
        devtools::collect_state_history(history, fresh, true);
        require(history.entries.size() == retained && !status.enabled,
                "Disabled capture recorded edits or erased retained history");
        devtools::set_state_capture(true);
        queue.push_back(event(now, 79, 80));
        devtools::collect_state_history(history, fresh, true);
        require(queue.empty() && history.entries.size() == retained,
                "Re-enable captured the disabled interval");
        queue.push_back(event(now + 1, 80, 81));
        devtools::collect_state_history(history, fresh);
        fresh.variables.front().value->raw_value = 81;
        devtools::collect_state_history(history, fresh, true);
        require(history.entries.size() == retained + 1 &&
                    history.entries.back().origin == devtools::StateChangeOrigin::native_action,
                "Fresh sampling duplicated the already observed native action");

        queue.push_back(event(now + 2, 81, 82));
        ++fresh.session;
        devtools::collect_state_history(history, fresh, true);
        require(history.entries.empty() && queue.empty() && armed.session == fresh.session,
                "Session change attributed stale queue data to the new session");
        devtools::clear_state_history(history);
        fresh.variables.front().value->raw_value = 82;
        devtools::collect_state_history(history, fresh, true);
        require(history.entries.empty(), "Clear recreated the discarded sampled interval");

        queue.push_back(event(now + 3, 82, 83));
        status.context_current = false;
        devtools::record_inspector_write(fresh, fresh.variables.front(), 83);
        devtools::collect_state_history(history, fresh);
        require(history.entries.empty() && !history.seeded && history.previous.empty() &&
                    devtools::state_capture_status().find(L"context changed") != std::wstring::npos,
                "Invalid live context retained held history or pending inspector edits");
        queue.push_back(event(now + 4, 83, 84));
        devtools::collect_state_history(history, fresh);
        require(history.entries.empty(), "Invalid context resumed without a fresh snapshot");
        ++fresh.session;
        devtools::collect_state_history(history, fresh, true);
        require(queue.empty() && history.seeded && armed.session == fresh.session &&
                    status.context_current,
                "Fresh snapshot did not reseed after stale held context");

        devtools::set_state_capture(false);
        attach_ok = false;
        devtools::set_state_capture(true);
        devtools::collect_state_history(history, fresh, true);
        fresh.variables.front().value->raw_value = 83;
        devtools::collect_state_history(history, fresh, true);
        require(devtools::state_capture_enabled() && history.entries.size() == 1 &&
                    history.entries.back().origin == devtools::StateChangeOrigin::snapshot &&
                    devtools::state_capture_status().find(L"unavailable") != std::wstring::npos,
                "Unavailable native hook removed sampling or misreported coverage");
        devtools::release_state_capture();
        require(!devtools::state_capture_enabled() && !status.enabled && releases == 1 &&
                    resets > 0,
                "Release retained active capture");
        std::cout << "State capture adapter checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        devtools::release_state_capture();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
