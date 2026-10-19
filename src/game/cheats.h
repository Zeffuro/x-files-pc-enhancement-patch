#pragma once
#include "functions/generated.h"
#include <windows.h>
#include <optional>
#include <span>
#include <string_view>

namespace native_game {
struct Cheat {
    std::wstring_view code;
    std::uint32_t trigger, actions;
};

struct CheatContext {
    HWND window = nullptr;
    std::uintptr_t application = 0, state = 0, view = 0;
    bool operator==(const CheatContext&) const = default;
};

std::span<const Cheat> cheats();
std::optional<std::size_t> cheat_index(std::wstring_view code);
std::optional<CheatContext> cheat_identity(const std::byte* image, const Profile& profile,
                                           HWND window);
std::optional<CheatContext> cheat_context(const std::byte* image, const Profile& profile,
                                          HWND window);
bool queue_cheat(const std::byte* image, const Profile& profile, const CheatContext& expected,
                 std::size_t index, NativeFunctions::QueueActionList callback = nullptr);
}
