#pragma once
#include <string_view>

namespace diagnostics {
void record_game_context() noexcept;
void record_tool_context(std::wstring_view text) noexcept;
}
