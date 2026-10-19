#pragma once
#include "game/cheats.h"
#include <cstdint>
#include <optional>

namespace devtools {
enum class EasterEggRequestStatus { waiting, ready, scene_changed, timed_out };
enum class EasterEggRequestKind { cheat, studio, studio_load };

struct EasterEggRequest {
    native_game::CheatContext context;
    std::size_t index = 0;
    std::optional<std::uint64_t> waiting_since;
    EasterEggRequestKind kind = EasterEggRequestKind::cheat;
};

inline EasterEggRequestStatus
poll_easter_egg_request(EasterEggRequest& request,
                        const std::optional<native_game::CheatContext>& current, bool exploring,
                        bool ready, std::uint64_t now) {
    if (!current || *current != request.context || !exploring) {
        return EasterEggRequestStatus::scene_changed;
    }
    if (request.waiting_since &&
        (now < *request.waiting_since || now - *request.waiting_since >= 2000)) {
        return EasterEggRequestStatus::timed_out;
    }
    if (ready) {
        return EasterEggRequestStatus::ready;
    }
    if (!request.waiting_since) {
        request.waiting_since = now;
    }
    return EasterEggRequestStatus::waiting;
}
}
