#include "enhancements/reveal_model.h"
#include <iostream>
#include <stdexcept>
#include <array>
#include "game/render/canvas_surface.h"

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void rendering() {
    using namespace enhancements;
    native_game::CanvasSurface canvas;
    const auto source = CreateCompatibleDC(nullptr);
    const auto screen_dc = GetDC(nullptr);
    const auto bitmap = CreateCompatibleBitmap(screen_dc, 640, 480);
    ReleaseDC(nullptr, screen_dc);
    const auto old_bitmap = SelectObject(source, bitmap);
    const auto dc = canvas.copy(source);
    require(dc != nullptr, "Cannot create reveal drawing surface");
    const auto pen = GetCurrentObject(dc, OBJ_PEN), brush = GetCurrentObject(dc, OBJ_BRUSH),
               font = GetCurrentObject(dc, OBJ_FONT);
    SetTextColor(dc, RGB(1, 2, 3));
    SetBkMode(dc, OPAQUE);
    std::array<std::vector<COLORREF>, 6> pixels;
    unsigned index = 0;
    for (const auto kind :
         {game::Interaction::click, game::Interaction::item, game::Interaction::unknown,
          game::Interaction::view, game::Interaction::talk, game::Interaction::use}) {
        RECT screen{0, 0, 640, 480};
        FillRect(dc, &screen, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const std::array items{reveal::Marker{{100, 100, 136, 128}, 0, kind}};
        reveal::draw(dc, items, {20, 90, 620, 330});
        require(GetCurrentObject(dc, OBJ_PEN) == pen && GetCurrentObject(dc, OBJ_BRUSH) == brush &&
                    GetCurrentObject(dc, OBJ_FONT) == font && GetTextColor(dc) == RGB(1, 2, 3) &&
                    GetBkMode(dc) == OPAQUE,
                "Reveal drawing changed the game's GDI state");
        for (int y = 100; y < 148; ++y) {
            for (int x = 80; x < 156; ++x) {
                pixels[index].push_back(GetPixel(dc, x, y));
            }
        }
        ++index;
    }
    require(pixels[0] != pixels[1] && pixels[0] != pixels[2] && pixels[1] != pixels[2],
            "Click, item and unknown markers are visually indistinguishable");
    require(pixels[3] != pixels[0] && pixels[4] != pixels[0] && pixels[3] != pixels[4],
            "Known View and Talk actions need distinct captions beside their markers");
    require(pixels[5] != pixels[0] && pixels[5] != pixels[1],
            "An ordinary Use cue must retain corners and its own caption");
    for (const auto kind :
         {game::Interaction::view, game::Interaction::talk, game::Interaction::use}) {
        RECT screen{0, 0, 640, 480};
        FillRect(dc, &screen, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const std::array items{reveal::Marker{{100, 100, 136, 128}, 0, kind}};
        reveal::draw(dc, items, {20, 90, 620, 330}, false);
        unsigned pixel_index = 0;
        for (int y = 100; y < 148; ++y) {
            for (int x = 80; x < 156; ++x) {
                require(GetPixel(dc, x, y) == pixels[0][pixel_index++],
                        "Disabling action labels must retain the marker and remove its caption");
            }
        }
    }
    {
        RECT screen{0, 0, 640, 480};
        const RECT viewport{0, 0, 640, 430};
        const std::array caption{reveal::Marker{{300, 0, 320, 16}, 0, game::Interaction::view}};
        FillRect(dc, &screen, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        reveal::draw(dc, caption, viewport);
        std::vector<COLORREF> before;
        for (int y = 18; y < 34; ++y) {
            for (int x = 280; x < 340; ++x) {
                before.push_back(GetPixel(dc, x, y));
            }
        }
        const std::array mixed{caption[0],
                               reveal::Marker{{500, 200, 520, 216}, 0, game::Interaction::click}};
        FillRect(dc, &screen, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        reveal::draw(dc, mixed, viewport);
        unsigned pixel_index = 0;
        for (int y = 18; y < 34; ++y) {
            for (int x = 280; x < 340; ++x) {
                require(GetPixel(dc, x, y) == before[pixel_index++],
                        "An in-scene legend painted over an action caption");
            }
        }
    }
    {
        RECT screen{0, 0, 640, 480};
        FillRect(dc, &screen, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const RECT narrow{105, 100, 131, 131};
        const std::array items{reveal::Marker{narrow, 0, game::Interaction::view}};
        reveal::draw(dc, items, narrow);
        for (int y = 80; y < 150; ++y) {
            for (int x = 80; x < 156; ++x) {
                if (x < narrow.left || x >= narrow.right || y < narrow.top || y >= narrow.bottom) {
                    require(GetPixel(dc, x, y) == RGB(0, 0, 0),
                            "A cramped caption escaped its viewport");
                }
            }
        }
    }
    for (const auto kind : {game::Interaction::item, game::Interaction::unknown}) {
        RECT screen{0, 0, 640, 480};
        FillRect(dc, &screen, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const std::array narrow{reveal::Marker{{440, 186, 450, 214}, 0, kind},
                                reveal::Marker{{0, 90, 2, 92}, 0, kind}};
        reveal::draw(dc, narrow, {0, 90, 640, 330});
        for (int y = 180; y < 220; ++y) {
            for (int x = 430; x < 460; ++x) {
                require((x >= 440 && x < 450 && y >= 186 && y < 214) ||
                            GetPixel(dc, x, y) == RGB(0, 0, 0),
                        "Narrow marker drew into an occluding target");
            }
        }
        require(GetPixel(dc, 2, 90) == RGB(0, 0, 0) && GetPixel(dc, 0, 92) == RGB(0, 0, 0),
                "Small edge marker exceeded its exposed region");
    }
    {
        RECT screen{0, 0, 640, 480};
        FillRect(dc, &screen, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const RECT exposed{0, 90, 5, 96};
        const std::array target{
            game::WorldTarget{exposed, exposed, 1, true, game::Interaction::move_left}};
        const auto items = reveal::markers(target, {0, 90, 640, 330}, false);
        require(items.size() == 1, "A narrow native movement target was lost");
        reveal::draw(dc, items, {0, 90, 640, 330});
        for (int y = 86; y < 100; ++y) {
            for (int x = 0; x < 12; ++x) {
                require((x < exposed.right && y >= exposed.top && y < exposed.bottom) ||
                            GetPixel(dc, x, y) == RGB(0, 0, 0),
                        "A narrow movement arrow escaped its exposed bounds");
            }
        }
    }
    RECT screen{0, 0, 640, 480};
    FillRect(dc, &screen, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const std::array bottom{reveal::Marker{{300, 410, 316, 422}, 0, game::Interaction::click}};
    reveal::draw(dc, bottom, {0, 50, 640, 430});
    require(GetPixel(dc, 300, 410) != RGB(0, 0, 0),
            "Tall viewport legend covered a bottom-strip interaction");
    FillRect(dc, &screen, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const RECT full_view{0, 0, 640, 430};
    const std::array occupied{reveal::Marker{full_view, 0, game::Interaction::click}};
    reveal::draw(dc, occupied, full_view);
    for (int y = 410; y < 440; ++y) {
        for (int x = 12; x < 628; ++x) {
            require(GetPixel(dc, x, y) == RGB(0, 0, 0),
                    "Exhausted legend placement drew over an occupied viewport");
        }
    }
    SelectObject(source, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(source);
}
}

int main() {
    using namespace enhancements;
    try {
        const RECT viewport{0, 50, 640, 430};
        const std::vector<game::WorldTarget> targets{
            {viewport, viewport, 1, false},
            {{0, 50, 50, 430}, {0, 50, 50, 430}, 2, true},
            {{250, 150, 300, 190}, {250, 150, 300, 190}, 3, false},
            {{252, 150, 302, 190}, {252, 150, 302, 190}, 4, false},
            {{400, 180, 450, 220}, {440, 180, 450, 220}, 5, false}};
        const auto all = reveal::markers(targets, viewport, false);
        require(all.size() == 3, "Catch-all filtering or marker overlap grouping failed");
        require(all[0].direction == 1 && all[0].bounds.right - all[0].bounds.left == 13,
                "Large navigation region did not become a small directional marker");
        require(all[1].bounds.right - all[1].bounds.left <= 36 &&
                    all[1].bounds.bottom - all[1].bounds.top <= 28,
                "Interaction marker exceeds its subtle size limit");
        require(all[2].bounds.left >= targets.back().exposed.left,
                "Partly occluded interaction marker moved into the blocked area");
        require(reveal::markers(targets, viewport, true).size() == 1,
                "Exits-only reveal included an interaction");
        require(reveal::markers(targets, RECT{}, false).empty(),
                "Invalid viewport produced markers");
        const std::vector<game::WorldTarget> scene{
            {{416, 120, 504, 273}, {416, 120, 504, 273}, 1, false, game::Interaction::item},
            {{352, 267, 405, 314}, {352, 267, 405, 314}, 2, false, game::Interaction::click}};
        const auto distinct = reveal::markers(scene, {20, 90, 620, 330}, false);
        require(distinct.size() == 2 && distinct[0].interaction == game::Interaction::item &&
                    distinct[1].interaction == game::Interaction::click,
                "Person item-use and body click distinction was lost");
        require(reveal::markers(scene, {20, 90, 620, 330}, true).empty(),
                "Item interaction leaked into exits-only reveal");
        const std::vector<game::WorldTarget> movement{
            {{20, 90, 152, 330}, {20, 90, 152, 330}, 1, true, game::Interaction::move_left},
            {{470, 90, 620, 330}, {470, 90, 620, 330}, 2, true, game::Interaction::move_right},
            {{170, 110, 240, 241}, {170, 110, 240, 241}, 3, false, game::Interaction::talk}};
        const auto exits = reveal::markers(movement, {20, 90, 620, 330}, true);
        require(exits.size() == 2 && exits[0].direction == 1 && exits[1].direction == 2,
                "Native movement cursor cues did not produce arrows in exits-only reveal");
        for (const auto kind : {game::Interaction::view, game::Interaction::talk,
                                game::Interaction::use, game::Interaction::item}) {
            const RECT wide{250, 90, 600, 330};
            const std::array target{game::WorldTarget{wide, wide, 1, true, kind}};
            const auto marked = reveal::markers(target, {20, 90, 620, 330}, false);
            require(marked.size() == 1 && marked[0].direction == 0 &&
                        marked[0].interaction == kind &&
                        marked[0].bounds.right - marked[0].bounds.left == 36,
                    "A known navigation-list action became a geometry arrow");
            require(reveal::markers(target, {20, 90, 620, 330}, true).empty(),
                    "A known navigation-list action leaked into exits-only reveal");
        }
        rendering();
        reveal::Hold held;
        require(held.update(true, true), "Initial hold did not reveal");
        require(held.update(true, true), "Stationary hold was lost");
        require(!held.update(true, false) && held.update(true, true),
                "Scene transition lost a continuous hold");
        held.suspend();
        require(!held.update(true, true), "Alt+Enter or disconnect did not block a held reveal");
        held.update(false, false);
        require(held.update(true, true), "Focus return did not rearm after release");
        std::cout << "Hotspot filtering and hold transitions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
