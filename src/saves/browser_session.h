#pragma once
#include "browser_state.h"
#include <algorithm>

namespace saves {
class LoadBrowserSession {
    BrowserCategory category_ = BrowserCategory::manual;
    std::array<BrowserPosition, 4> positions_{};

public:
    void remember(const Browser& state, bool enabled) {
        if (!enabled) {
            *this = {};
        } else if (!state.saving) {
            category_ = state.category;
            positions_ = state.positions;
            positions_[static_cast<unsigned>(category_)] = {state.page, state.selection};
        }
    }

    void restore(Browser& state, bool enabled) {
        if (!enabled) {
            *this = {};
            return;
        }
        if (state.saving) {
            return;
        }
        state.positions = positions_;
        for (unsigned i = 0; i < state.positions.size(); ++i) {
            state.category = static_cast<BrowserCategory>(i);
            auto& position = state.positions[i];
            position.page = std::min(position.page, browser_page_count(state) - 1);
            const auto size =
                state.category == BrowserCategory::manual
                    ? static_cast<std::size_t>(slot_pages * slots_per_page)
                : state.category == BrowserCategory::existing  ? state.legacy.entries.size()
                : state.category == BrowserCategory::quicksave ? state.quicksaves.size()
                                                               : state.autosaves.size();
            const auto first = static_cast<std::size_t>(position.page) * slots_per_page;
            const auto cards =
                first < size ? std::min<std::size_t>(slots_per_page, size - first) : 1;
            position.selection = std::min(position.selection, static_cast<unsigned>(cards - 1));
        }
        state.category = category_;
        const auto position = state.positions[static_cast<unsigned>(category_)];
        state.page = position.page;
        state.selection = position.selection;
        state.focus = static_cast<int>(position.selection);
    }
};
}
