#include "transcript/history.h"
#include "transcript/selection.h"

#include <stdexcept>

namespace {
void require(bool value, const char* reason = "Transcript history invariant failed") {
    if (!value) {
        throw std::runtime_error(reason);
    }
}
}

int main() {
    transcript::Selection selection;
    selection.begin(L"Actual choice", 100);
    require(selection.update(false, 101).empty(), "Mouse-down alone recorded a choice");
    selection.release(102);
    require(selection.update(false, 102).empty(), "Mouse-up alone recorded a choice");
    require(selection.update(true, 103) == L"Actual choice",
            "Deferred native acceptance lost the choice");
    require(selection.update(true, 104).empty(), "Native redraw duplicated the choice");
    selection.begin(L"Canceled", 200);
    selection.cancel();
    require(selection.update(true, 201).empty(), "Panel close recorded a canceled choice");
    selection.begin(L"Expired", 300);
    selection.release(300);
    require(selection.update(true, 1300).empty(),
            "Unrelated later transition recorded a stale choice");
    selection.begin(L"Held choice", 2000);
    require(selection.update(false, 4000).empty() && selection.pending(),
            "Held click expired before release");
    selection.release(4001);
    require(selection.update(true, 4002) == L"Held choice",
            "Long held choice lost deferred acceptance");
    transcript::History history;
    history.record_choice(L"Ask about the case");
    history.record_caption(L"xv/19808.xmv", 120, 600, 600, L"A.D. Skinner.");
    history.record_caption(L"xv/19808.xmv", 120, 600, 600, L"A.D. Skinner.");
    require(history.entries().size() == 2);
    require(history.entries().front().kind == transcript::Kind::Choice);

    history.record_caption(L"xv/19808.xmv", 1200, 1800, 600, L"A.D. Skinner.");
    history.record_caption(L"xv/19810.xmv", 120, 600, 600, L"A.D. Skinner.");
    require(history.entries().size() == 4);
    history.record_caption(L"xv/19808.xmv", 120, 600, 600, L"Corrected caption.");
    require(history.entries().size() == 5);
    history.record_choice(L"Ask about the case again");
    history.record_caption(L"xv/19808.xmv", 120, 600, 600, L"A.D. Skinner.");
    history.record_caption(L"xv/19808.xmv", 120, 600, 600, L"A.D. Skinner.");
    require(history.entries().size() == 7);

    history.record_caption(L"xv/19808.xmv", 10, 10, 600, L"Invalid");
    history.record_caption(L"xv/19808.xmv", 10, 20, 0, L"Invalid");
    history.record_caption(L"", 10, 20, 600, L"Invalid");
    history.record_choice(L"");
    require(history.entries().size() == 7);
    history.record_marker(L"Saved game loaded");
    require(history.entries().back().kind == transcript::Kind::Marker);
    history.record_caption(L"xv/19808.xmv", 120, 600, 600, L"A.D. Skinner.");
    require(history.entries().size() == 9);
    require(history.evicted() == 0);

    for (std::size_t i = 0; i < transcript::History::capacity; ++i) {
        history.record_choice(std::to_wstring(i));
    }
    require(history.entries().size() == transcript::History::capacity);
    require(history.entries().front().text == L"0");
    require(history.entries().back().text == std::to_wstring(transcript::History::capacity - 1));
    require(history.evicted() == 9);
    history.clear();
    require(history.entries().empty());
    require(history.evicted() == 0);
}
