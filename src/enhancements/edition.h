#pragma once

#include "game/profiles/generated.h"

namespace enhancements::game {

using Edition = native_game::Profile;
inline constexpr Edition dvd = native_game::profile_dvd_20000;
inline constexpr Edition cd = native_game::profile_cd_10012;

inline const Edition* edition_named(std::string_view name) {
    return name == "DVD" ? &dvd : name == "CD" ? &cd : nullptr;
}

const Edition& edition();

}
