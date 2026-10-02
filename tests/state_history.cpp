#include "devtools/state_history.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

devtools::GameSnapshot snapshot() {
    devtools::GameSnapshot value;
    value.manager = 1;
    value.state = 2;
    value.hdb = 3;
    value.application = 4;
    value.session = 5;
    value.variables = {
        {{0x53, 42, true}, L"Location", L"", devtools::DatabaseVariable{65, 0x80}, 100}};
    return value;
}

devtools::StateHistoryEntry event(int before, int after) {
    devtools::StateHistoryEntry value;
    value.key = devtools::DatabaseObjectKey{0x53, 42, true};
    value.address = 100;
    value.name = L"Location";
    value.before = before;
    value.after = after;
    value.type_flags = 0x80;
    value.origin = devtools::StateChangeOrigin::native_action;
    value.caller_rva = 0x12345;
    value.context = L"Action 7 / Assign";
    return value;
}
}

int main() {
    try {
        devtools::StateHistory history;
        auto current = snapshot();
        require(devtools::observe_state_history(history, current, 1), "First sample did not seed");
        require(history.entries.empty(), "Initial values became changes");
        current.variables[0].value->raw_value = 66;
        devtools::observe_state_history(history, current, 10);
        require(history.entries.size() == 1 && history.entries.front().before == 65 &&
                    history.entries.front().after == 66 && history.entries.front().ticks == 10 &&
                    history.entries.front().origin == devtools::StateChangeOrigin::snapshot &&
                    !history.entries.front().caller_rva,
                "Sample attribution or scalar diff incorrect");

        devtools::append_state_history(history, event(66, 67));
        devtools::append_state_history(history, event(67, 68));
        current.variables[0].value->raw_value = 68;
        devtools::observe_state_history(history, current, 20);
        require(history.entries.size() == 3, "Known complete change chain duplicated by sampling");
        devtools::append_state_history(history, event(69, 70));
        current.variables[0].value->raw_value = 70;
        devtools::observe_state_history(history, current, 30);
        require(history.entries.size() == 5 && history.entries.back().before == 68 &&
                    history.entries.back().origin == devtools::StateChangeOrigin::snapshot,
                "Partial chain falsely attributed a missing writer");

        devtools::clear_state_history(history);
        current.variables[0].value->raw_value = 71;
        devtools::observe_state_history(history, current, 40);
        require(history.entries.empty(), "Clear recreated discarded changes");
        current.variables[0].address = 101;
        current.variables[0].value->raw_value = 72;
        devtools::observe_state_history(history, current, 50);
        require(history.entries.empty(), "Replacement object treated as mutation");
        current.variables.push_back(current.variables.front());
        current.variables[0].value->raw_value = 73;
        devtools::observe_state_history(history, current, 60);
        current.variables.pop_back();
        current.variables[0].value->raw_value = 74;
        devtools::observe_state_history(history, current, 70);
        require(history.entries.empty(), "Ambiguous source produced a change");
        current.variables[0].value.reset();
        devtools::observe_state_history(history, current, 80);
        current.variables[0].value = devtools::DatabaseVariable{75, 0x80};
        devtools::observe_state_history(history, current, 90);
        require(history.entries.empty(), "Unreadable scalar treated as zero");

        for (unsigned index = 0; index < devtools::StateHistory::capacity + 4; ++index) {
            devtools::append_state_history(history, event(index, index + 1));
        }
        require(history.entries.size() == 512 && history.dropped == 4 &&
                    history.entries.back().sequence - history.entries.front().sequence == 511,
                "History cap or monotonic sequence failed");
        const auto copied = devtools::state_history_text(history.entries.back());
        require(copied.find(L"State 83:42") != copied.npos &&
                    copied.find(L"Game +0x00012345") != copied.npos &&
                    copied.find(L"Action 7 / Assign") != copied.npos,
                "Copy omitted identity or provenance");
        for (auto member : {&devtools::GameSnapshot::manager, &devtools::GameSnapshot::state,
                            &devtools::GameSnapshot::hdb, &devtools::GameSnapshot::application,
                            &devtools::GameSnapshot::session}) {
            devtools::append_state_history(history, event(1, 2));
            ++(current.*member);
            require(devtools::observe_state_history(history, current, 100) &&
                        history.entries.empty() && history.dropped == 0,
                    "Native context transition retained stale history");
        }
        auto hdb = current.variables.front();
        hdb.key.state_database = false;
        auto other_class = current.variables.front();
        other_class.key.class_id = 0x54;
        current.variables.push_back(hdb);
        current.variables.push_back(other_class);
        devtools::observe_state_history(history, current, 110);
        for (auto& row : current.variables) {
            ++row.value->raw_value;
        }
        devtools::observe_state_history(history, current, 120);
        require(history.entries.size() == 3, "Source or class identities collided");
        std::cout << "State history checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
