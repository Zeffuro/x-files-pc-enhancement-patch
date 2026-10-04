#pragma once
#include "game_ui.h"
#include <span>

namespace enhancements::reveal {
struct Marker {
    RECT bounds;
    int direction = 0;
    game::Interaction interaction = game::Interaction::unknown;
};

std::vector<Marker> markers(std::span<const game::WorldTarget> targets, RECT viewport,
                            bool exits_only);
void draw(HDC dc, std::span<const Marker> items, RECT viewport, bool show_labels = true);

class Hold {
public:
    bool update(bool held, bool available);
    void suspend();

private:
    bool blocked_ = false;
};
}
