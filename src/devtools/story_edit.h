#pragma once
#include "game_state.h"

namespace devtools {
std::optional<std::int32_t> parse_state_value(std::wstring_view text);
std::wstring validate_state_edit(const GameSnapshot& expected, const GameSnapshot& current,
                                 const StateVariable& variable, std::int32_t value, bool enabled);
std::wstring write_state_variable(const std::byte* image, const native_game::Profile& profile,
                                  const GameSnapshot& expected, const GameSnapshot& current,
                                  const StateVariable& variable, std::int32_t value, bool enabled);
}
