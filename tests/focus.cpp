#include "enhancements/focus.h"
#include "enhancements/spring_cursor.h"

#include <array>
#include <iostream>
#include <stdexcept>

int main() {
    using enhancements::directional_target;
    try {
        const auto require = [](bool condition, const char* message) {
            if (!condition) {
                throw std::runtime_error(message);
            }
        };
        constexpr std::array<RECT, 4> grid{{
            {10, 10, 30, 30},
            {60, 10, 80, 30},
            {10, 60, 30, 80},
            {60, 60, 80, 80},
        }};
        require(directional_target(grid, {20, 20}, 1, 0) == 1, "Right must stay on the row.");
        require(directional_target(grid, {20, 20}, 0, 1) == 2, "Down must stay in the column.");
        require(directional_target(grid, {70, 70}, 1, 0) == 2,
                "Row wrap crossed into another row.");
        require(directional_target(grid, {70, 70}, 0, 1) == 1, "Column wrap crossed columns.");
        require(directional_target(grid, {0, 0}, 0, 1) == 0,
                "Unfocused forward entry is not first.");
        require(directional_target(grid, {0, 0}, 0, -1) == 3,
                "Unfocused reverse entry is not last.");
        require(directional_target(grid, {20, 20}, 0, 0) == -1, "Idle input moved focus.");
        require(directional_target({}, {20, 20}, 1, 0) == -1, "Empty page selected a target.");
        for (const bool can_save : {false, true}) {
            const auto menu = enhancements::main_menu_targets(can_save);
            const auto down = directional_target(menu, {555, 147}, 0, 1);
            require(down >= 0 && menu[down].top == (can_save ? 175 : 225),
                    "Menu navigation stopped on disabled Save or skipped enabled Save.");
            const auto up = directional_target(menu, {555, 247}, 0, -1);
            require(up >= 0 && menu[up].top == (can_save ? 175 : 125),
                    "Reverse menu navigation did not follow Save availability.");
        }
        const auto& tabs = enhancements::pda_toolbar;
        for (int index = 0; index < static_cast<int>(tabs.size()); ++index) {
            const POINT cursor{(tabs[index].left + tabs[index].right) / 2, 399};
            require(directional_target(tabs, cursor, 1, 0) == (index + 1) % 5,
                    "PDA tab transition reset forward navigation.");
            require(directional_target(tabs, cursor, -1, 0) == (index + 4) % 5,
                    "PDA tab transition reset backward navigation.");
        }
        require(directional_target(std::span(grid).first(1), {20, 20}, 1, 0) == 0,
                "Single-button page lost focus.");
        constexpr std::array<RECT, 3> irregular{{
            {100, 10, 200, 30},
            {100, 60, 200, 80},
            {201, 35, 211, 45},
        }};
        require(directional_target(irregular, {150, 20}, 0, 1) == 1,
                "Closer diagonal control stole vertical list navigation.");
        using enhancements::hotspot_target;
        require(hotspot_target(grid, {40, 70}, 1) == 3,
                "Hotspot entry must choose the nearest target in the requested direction.");
        require(hotspot_target(grid, {70, 70}, 1) == 2,
                "Hotspot wrap must return to the opposite edge.");
        require(hotspot_target(grid, {20, 20}, -1) == 1,
                "Reverse hotspot wrap selected the wrong edge.");
        require(hotspot_target({}, {0, 0}, 1) == -1, "Empty scene selected a hotspot.");
        require(hotspot_target(grid, {20, 20}, 0) == -1, "Idle hotspot input moved the cursor.");
        constexpr std::array<RECT, 3> nested{
            {{0, 0, 100, 100}, {30, 30, 50, 50}, {60, 30, 80, 50}}};
        require(hotspot_target(nested, {40, 40}, 1) == 0,
                "A nested hotspot did not use the smallest containing target as its origin.");
        using enhancements::exposed_target;
        RECT exposed{};
        constexpr RECT exit{500, 90, 620, 330};
        constexpr std::array<RECT, 2> obstacles{{{504, 144, 584, 213}, {501, 292, 618, 328}}};
        require(exposed_target(exit, obstacles, exposed), "Partly covered exit was lost.");
        const POINT landing{(exposed.left + exposed.right) / 2, (exposed.top + exposed.bottom) / 2};
        require(PtInRect(&exit, landing) && !PtInRect(&obstacles[0], landing) &&
                    !PtInRect(&obstacles[1], landing),
                "Exit landing point was intercepted by a picture or computer.");
        const std::array<RECT, 1> covered{{exit}};
        require(!exposed_target(exit, covered, exposed),
                "Fully covered target remained selectable.");
        require(exposed_target(exit, {}, exposed) && EqualRect(&exposed, &exit),
                "Unobstructed hotspot bounds changed.");
        enhancements::SpringCursor spring;
        constexpr RECT screen{0, 0, 640, 480};
        POINT cursor{12, 34};
        require(!spring.update(0, 0, screen, cursor) && cursor.x == 12,
                "Idle spring pointer stole focus.");
        require(spring.update(32767, 32767, screen, cursor) && cursor.x == 639 && cursor.y == 0,
                "Spring pointer cannot reach top-right corner.");
        require(spring.update(-32768, -32768, screen, cursor) && cursor.x == 0 && cursor.y == 479,
                "Spring pointer cannot reach bottom-left corner.");
        require(spring.update(0, 0, screen, cursor) && cursor.x == 320 && cursor.y == 240,
                "Releasing the stick did not return to centre.");
        cursor = {12, 34};
        require(!spring.update(7849, -7849, screen, cursor) && cursor.x == 12,
                "Deadzone drift displaced a selected control.");
        spring.suspend();
        require(!spring.update(32767, 0, screen, cursor) && cursor.x == 12,
                "Held stick overrode hotspot selection.");
        require(!spring.update(0, 0, screen, cursor), "Suspended release stole focus.");
        require(spring.update(20308, 0, screen, cursor) && cursor.x == 480 && cursor.y == 240,
                "Spring pointer did not resume with proportional displacement.");
        std::cout << "Controller grid, list, wrap, spring pointer and idle checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
