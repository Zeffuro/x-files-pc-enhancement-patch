#pragma once

#include <windows.h>
#include <span>
#include <string_view>
#include <functional>
#include <cstdint>

namespace devtools {
enum class EasterEggShortcut { none, studio };

struct EasterEgg {
    std::wstring_view name;
    std::wstring_view code;
    std::wstring_view instructions;
    std::uint32_t variable = 0;
    EasterEggShortcut shortcut = EasterEggShortcut::none;
};

struct EasterEggActions {
    std::function<void(std::wstring_view)> use;
    std::function<void(std::uint32_t)> variable;
    std::function<void(EasterEggShortcut)> shortcut;
};

std::span<const EasterEgg> easter_eggs();
HWND create_easter_eggs(HWND parent, HMODULE module, HFONT font, EasterEggActions actions = {});
void update_easter_egg_scores(HWND panel, std::wstring_view text);
void enable_easter_egg_use(HWND panel, bool available);
}
