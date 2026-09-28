#include "enhancements/script_controls.h"
#include "enhancements/game_resources.h"
#include "enhancements/focus.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace {
using namespace enhancements;

struct Control {
    unsigned id;
    RECT bounds;
    unsigned clicks;
};

constexpr Control audio_controls[]{
    {0x7ff74, {4, 446, 185, 476}, 1},   {0x7ff74, {206, 446, 374, 476}, 1},
    {0x7ff74, {411, 445, 611, 474}, 1}, {0x7ff74, {480, 102, 623, 134}, 1},
    {0x7ff74, {416, 72, 544, 102}, 1},  {0x7ff74, {382, 39, 467, 71}, 1},
    {0x7ffd8, {0, 0, 640, 480}, 0},     {0x8000a, {23, 380, 74, 427}, 0},
    {0x7ff75, {351, 36, 461, 76}, 0},   {0x7fe74, {4, 446, 185, 476}, 1},
    {0x7fe74, {206, 446, 374, 476}, 1}, {0x7fe74, {160, 308, 169, 324}, 1},
    {0x7fe74, {151, 308, 160, 324}, 1}, {0x7fe74, {142, 308, 151, 324}, 1},
    {0x7fe74, {133, 308, 142, 324}, 1}, {0x7fe74, {124, 308, 133, 324}, 1},
    {0x7fe74, {115, 308, 124, 324}, 1}, {0x7fe74, {106, 308, 115, 324}, 1},
    {0x7fe74, {97, 308, 106, 324}, 1},  {0x7fe74, {88, 308, 97, 324}, 1},
    {0x7fe74, {79, 308, 88, 324}, 1},   {0x7fe74, {160, 260, 169, 276}, 1},
    {0x7fe74, {151, 260, 160, 276}, 1}, {0x7fe74, {142, 260, 151, 276}, 1},
    {0x7fe74, {133, 260, 142, 276}, 1}, {0x7fe74, {124, 260, 133, 276}, 1},
    {0x7fe74, {115, 260, 124, 276}, 1}, {0x7fe74, {106, 260, 115, 276}, 1},
    {0x7fe74, {97, 260, 106, 276}, 1},  {0x7fe74, {87, 260, 96, 276}, 1},
    {0x7fe74, {79, 260, 88, 276}, 1},   {0x7fe74, {160, 212, 169, 228}, 1},
    {0x7fe74, {151, 212, 160, 228}, 1}, {0x7fe74, {142, 212, 151, 228}, 1},
    {0x7fe74, {133, 212, 142, 228}, 1}, {0x7fe74, {124, 212, 133, 228}, 1},
    {0x7fe74, {115, 212, 124, 228}, 1}, {0x7fe74, {106, 212, 115, 228}, 1},
    {0x7fe74, {97, 212, 106, 228}, 1},  {0x7fe74, {88, 212, 97, 228}, 1},
    {0x7fe74, {79, 212, 88, 228}, 1},   {0x7fe74, {160, 164, 169, 180}, 1},
    {0x7fe74, {151, 164, 160, 180}, 1}, {0x7fe74, {142, 164, 151, 180}, 1},
    {0x7fe74, {133, 164, 142, 180}, 1}, {0x7fe74, {124, 164, 133, 180}, 1},
    {0x7fe74, {115, 164, 124, 180}, 1}, {0x7fe74, {106, 164, 115, 180}, 1},
    {0x7fe74, {97, 164, 106, 180}, 1},  {0x7fe74, {88, 164, 97, 180}, 1},
    {0x7fe74, {79, 164, 88, 180}, 1},   {0x7fe74, {57, 308, 73, 324}, 1},
    {0x7fe74, {57, 260, 73, 276}, 1},   {0x7fe74, {57, 212, 73, 228}, 1},
    {0x7fe74, {57, 164, 73, 180}, 1},   {0x7fe74, {176, 308, 192, 324}, 1},
    {0x7fe74, {176, 260, 192, 276}, 1}, {0x7fe74, {176, 212, 192, 228}, 1},
    {0x7fe74, {176, 164, 192, 180}, 1}, {0x7fe74, {51, 355, 85, 373}, 1},
    {0x7fed8, {40, 153, 466, 377}, 0},  {0x7ff0a, {404, 321, 445, 361}, 0},
    {0x7fe76, {59, 361, 73, 367}, 0},   {0x7fe75, {146, 168, 147, 175}, 0},
    {0x7fe75, {146, 264, 147, 271}, 0}, {0x7fe75, {110, 216, 111, 223}, 0},
    {0x7fe75, {137, 312, 138, 319}, 0}, {0x7ff0a, {41, 410, 71, 428}, 1},
};

template <class T> void put(std::byte* target, std::size_t offset, const T& value) {
    std::memcpy(target + offset, &value, sizeof(value));
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check(const game::Edition& profile) {
    std::vector<std::byte> image(0x300000), state(0x300), root(0x30), resource(8);
    std::array<std::array<std::byte, 0x1c0>, std::size(audio_controls)> controls{};
    std::array<game::List<std::byte>::Node, std::size(audio_controls)> nodes{};
    put(root.data(), 0, image.data() + profile.script_root);
    put(root.data(), 0x18, resource.data());
    put(resource.data(), 4, enhancements::resource::options);
    game::List<std::byte>::Node root_node{root.data(), nullptr, nullptr};
    game::List<std::byte> roots{nullptr, 1, &root_node, &root_node, nullptr};
    put(state.data(), 0x25c, roots);
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const auto& native = audio_controls[i];
        auto* object = controls[i].data();
        put(object, 0, image.data() + profile.script_control);
        put(object, 8, 1u);
        put(object, 0x144, native.id);
        put(object, profile.control_rectangle + 4, native.bounds);
        put(object, 0x20, native.clicks);
        nodes[i] = {object, i + 1 < nodes.size() ? &nodes[i + 1] : nullptr,
                    i ? &nodes[i - 1] : nullptr};
    }
    game::List<std::byte> inputs{nullptr, static_cast<unsigned>(nodes.size()), nodes.data(),
                                 &nodes.back(), nullptr};
    put(state.data(), 0x270, inputs);
    const auto result = game::read_script_controls(state.data(), image.data(), profile);
    require(!result.script_dialog, "Options content was mistaken for a modal dialog.");
    auto targets = result.buttons;
    const RECT invisible{41, 410, 71, 428};
    require(std::none_of(targets.begin(), targets.end(),
                         [&](const RECT& r) { return EqualRect(&r, &invisible); }),
            "An option with no artwork remains a controller target.");
    std::sort(targets.begin(), targets.end(), [](const RECT& a, const RECT& b) {
        return a.top + a.bottom == b.top + b.bottom ? a.left + a.right < b.left + b.right
                                                    : a.top + a.bottom < b.top + b.bottom;
    });
    targets.erase(std::unique(targets.begin(), targets.end(),
                              [](const RECT& a, const RECT& b) { return EqualRect(&a, &b); }),
                  targets.end());
    for (const auto& expected : std::span(audio_controls).first(6)) {
        require(std::any_of(targets.begin(), targets.end(),
                            [&](const RECT& r) { return EqualRect(&r, &expected.bounds); }),
                "An options tab, preset or Return target is absent.");
    }
    std::vector<bool> reachable(targets.size());
    reachable[0] = true;
    for (std::size_t pass = 0; pass < targets.size(); ++pass) {
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (!reachable[i]) {
                continue;
            }
            const auto& r = targets[i];
            const POINT point{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
            for (const POINT direction : {POINT{1, 0}, POINT{-1, 0}, POINT{0, 1}, POINT{0, -1}}) {
                const auto next = directional_target(targets, point, direction.x, direction.y);
                if (next >= 0) {
                    reachable[next] = true;
                }
            }
        }
    }
    require(std::all_of(reachable.begin(), reachable.end(), [](bool v) { return v; }),
            "D-pad cannot reach every audio control and outer navigation target.");
    std::array<std::byte, 8> visible_graphic{};
    put(controls.back().data(), 0x140, visible_graphic.data());
    const auto restored = game::read_script_controls(state.data(), image.data(), profile);
    require(std::any_of(restored.buttons.begin(), restored.buttons.end(),
                        [&](const RECT& r) { return EqualRect(&r, &invisible); }),
            "A visible option was removed from controller navigation.");
    put(resource.data(), 4, 0u);
    require(game::read_script_controls(state.data(), image.data(), profile).script_dialog,
            "Ordinary popup dialog detection was disabled.");
}
}

int main() {
    for (const auto* profile :
         {&game::dvd, &game::cd, &native_game::profile_cd_10019, &native_game::profile_cd_10020}) {
        check(*profile);
    }
}
