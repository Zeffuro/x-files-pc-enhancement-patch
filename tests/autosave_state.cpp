#include "enhancements/autosave_state.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}
}

int main() {
    try {
        using enhancements::SaveIdentity;
        enhancements::AutosaveState state;
        const SaveIdentity first{1, 2, L"nav1.nmv#1"}, next{1, 2, L"nav1.nmv#2"};
        const auto update = [&](std::uint64_t now, const std::optional<SaveIdentity>& scene,
                                bool blocked = false, bool enabled = true) {
            return state.update(now, enabled, blocked, scene);
        };
        require(!update(0, first) && !update(1999, first) && update(2000, first),
                "Initial exploration did not wait for stability");
        require(!update(100000, first), "Stationary scene repeatedly autosaved");
        require(!update(100001, next) && !update(101000, first) && !update(102000, next) &&
                    !update(103999, next) && update(104000, next),
                "Rapid transitions bypassed stability");
        require(!update(105000, std::nullopt) && !update(106000, next) && !update(108000, next) &&
                    !update(133999, next) && update(134000, next),
                "Return from unsafe gameplay bypassed write cooldown or lost checkpoint");
        require(!update(140000, first, true) && !update(142000, next) && !update(180000, next),
                "Focus or overlay interruption created another checkpoint");
        require(!update(180001, first) && !update(182000, first, true) && !update(182001, first) &&
                    !update(184000, first) && update(184001, first),
                "Blocked time counted toward stable exploration");
        state.loaded(200000);
        require(!update(200001, next) && !update(229999, next) && update(230000, next),
                "Loaded scene immediately overwrote rolling history");
        require(!update(250000, first, false, false) && !update(260000, first, false, false) &&
                    !update(270000, first) && update(272000, first),
                "Disabled autosaves ran or enable did not require stability");
        enhancements::AutosaveState failed;
        require(!failed.update(0, true, false, first) && failed.update(2000, true, false, first) &&
                    !failed.update(40000, true, false, first),
                "Failed save attempt repeatedly retried stationary scene");
        std::cout << "Autosave stability, suspension, cooldown and load boundaries passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
