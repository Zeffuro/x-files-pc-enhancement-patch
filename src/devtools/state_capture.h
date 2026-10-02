#pragma once
#include "state_history.h"

namespace devtools {
std::wstring set_state_capture(bool enabled);
bool state_capture_enabled();
std::wstring state_capture_status();
void collect_state_history(StateHistory& history, const GameSnapshot& snapshot,
                           bool refreshed = false);
void release_state_capture();
void record_inspector_write(const GameSnapshot& snapshot, const StateVariable& variable,
                            std::int32_t after);
}
