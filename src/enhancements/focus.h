#pragma once

#include <windows.h>
#include <span>
#include <array>
#include <vector>

namespace enhancements {

inline constexpr RECT workstation_media_field{377, 50, 609, 67};

inline constexpr std::array<RECT, 5> pda_toolbar{{
    {234, 386, 260, 412},
    {268, 386, 294, 412},
    {302, 386, 328, 412},
    {337, 386, 363, 412},
    {371, 386, 397, 412},
}};

std::vector<RECT> main_menu_targets(bool can_save);
int directional_target(std::span<const RECT> targets, POINT cursor, int horizontal, int vertical);
int hotspot_target(std::span<const RECT> targets, POINT cursor, int direction);
bool exposed_target(const RECT& bounds, std::span<const RECT> occluders, RECT& target);
bool point_controller(HWND window, const RECT& target, bool activate = false, bool right = false);

}
