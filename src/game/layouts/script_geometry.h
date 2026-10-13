#pragma once
#include "game/resources.h"
#include <windows.h>
#include <optional>

namespace native_game::script_geometry {
inline constexpr SIZE logical_size{640, 480};
inline constexpr unsigned photo_columns = 5, maximum_thumbnails = 20;
inline constexpr RECT photo_grid{135, 158, 565, 429};
inline constexpr POINT photo_preview{420, 32};

struct Arrows {
    POINT up, down;
};

inline constexpr Arrows photo_arrows{{575, 167}, {574, 420}};

inline std::optional<Arrows> text_arrows(unsigned id) {
    switch (id) {
        case resource::pda_notes:
            return Arrows{{422, 95}, {422, 339}};
        case resource::pda_message:
            return Arrows{{428, 62}, {428, 351}};
        case resource::workstation_message:
            return Arrows{{607, 107}, {607, 443}};
        case resource::workstation_search:
        case resource::workstation_search_capture:
            return Arrows{{309, 373}, {309, 456}};
        default:
            return std::nullopt;
    }
}
}
