#include "browser_state.h"
#include <algorithm>
#include <limits>

namespace saves {
unsigned browser_page_count(const Browser& state) {
    if (state.saving || state.category == BrowserCategory::manual) {
        return slot_pages;
    }
    const auto size = state.category == BrowserCategory::existing    ? state.legacy.entries.size()
                      : state.category == BrowserCategory::quicksave ? state.quicksaves.size()
                                                                     : state.autosaves.size();
    const auto pages = (size + slots_per_page - 1) / slots_per_page;
    return static_cast<unsigned>(std::max<std::size_t>(1, pages));
}

void change_browser_page(Browser& state, int direction) {
    const auto count = static_cast<int>(browser_page_count(state));
    const auto page = direction <= -static_cast<int>(slot_pages) ? 0
                      : direction >= static_cast<int>(slot_pages)
                          ? count - 1
                          : (static_cast<int>(state.page) + direction + count) % count;
    state.page = static_cast<unsigned>(page);
    load_browser_page(state);
    state.status.clear();
}

void cycle_browser_category(Browser& state) {
    if (state.saving || state.confirm || state.keyboard) {
        return;
    }
    auto index = static_cast<unsigned>(state.category);
    state.positions[index] = {state.page, state.selection};
    index = (index + 1) % static_cast<unsigned>(state.positions.size());
    state.category = static_cast<BrowserCategory>(index);
    state.page = std::min(state.positions[index].page, browser_page_count(state) - 1);
    state.selection = state.positions[index].selection;
    state.focus = 8;
    load_browser_page(state);
    state.status.clear();
}

RECT card_rect(unsigned index) {
    const auto x = 72L + static_cast<LONG>(index % 3) * 170;
    const auto y = 94L + static_cast<LONG>(index / 3) * 125;
    return {x, y, x + 156, y + 113};
}

RECT control_rect(int item) {
    if (item >= 0 && item < 6) {
        return card_rect(static_cast<unsigned>(item));
    }
    constexpr std::array<RECT, 7> buttons{{{68, 341, 148, 365},
                                           {492, 341, 572, 365},
                                           {245, 341, 395, 365},
                                           {164, 373, 476, 395},
                                           {190, 401, 292, 429},
                                           {350, 401, 478, 431},
                                           {502, 402, 584, 430}}};
    return item >= 6 && item <= 12 ? buttons[item - 6] : RECT{};
}

bool control_enabled(const Browser& state, int item) {
    if (state.confirm) {
        return item == 10 || item == 11;
    }
    if (item == 8) {
        return !state.saving;
    }
    if (item == 9) {
        return state.saving && state.category == BrowserCategory::manual;
    }
    if (item == 11) {
        return (state.saving && state.category == BrowserCategory::manual) ||
               state.slots[state.selection].readable;
    }
    if (item == 12) {
        return state.category == BrowserCategory::manual && state.slots[state.selection].occupied;
    }
    return item >= 0 && item <= 12;
}

void cycle_browser_focus(Browser& state, int direction) {
    for (int attempt = 0; attempt < 13; ++attempt) {
        state.focus = (state.focus + direction + 13) % 13;
        if (control_enabled(state, state.focus)) {
            return;
        }
    }
}

void move_browser_focus(Browser& state, int horizontal, int vertical) {
    const auto current = control_rect(state.focus);
    const int cx = (current.left + current.right) / 2;
    const int cy = (current.top + current.bottom) / 2;
    int nearest = -1;
    long best = std::numeric_limits<long>::max();
    for (int item = 0; item < 13; ++item) {
        if (item == state.focus || !control_enabled(state, item)) {
            continue;
        }
        const auto target = control_rect(item);
        const int dx = (target.left + target.right) / 2 - cx;
        const int dy = (target.top + target.bottom) / 2 - cy;
        const int forward = horizontal ? dx * horizontal : dy * vertical;
        const int across = horizontal ? std::abs(dy) : std::abs(dx);
        if (forward > 0) {
            const long score = forward + across * 4;
            if (score < best) {
                best = score;
                nearest = item;
            }
        }
    }
    if (nearest >= 0) {
        state.focus = nearest;
    }
}

RECT name_key_rect(unsigned key) {
    if (key < 40) {
        const auto x = 111L + static_cast<LONG>(key % 10) * 42;
        const auto y = 258L + static_cast<LONG>(key / 10) * 27;
        return {x, y, x + 39, y + 24};
    }
    const auto x = 111L + static_cast<LONG>(key - 40) * 105;
    return {x, 370, x + 102, 397};
}
}
