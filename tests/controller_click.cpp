#include "enhancements/controller_click.h"

#include <cstdlib>

namespace {
void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}
}

int main() {
    using enhancements::input::ClickDispatch;

    ClickDispatch normal;
    normal.queue();
    require(!normal.in_flight());
    normal.begin_down();
    require(normal.in_flight());
    require(!normal.end_down());
    require(!normal.in_flight());
    require(normal.pending());
    normal.begin_up();
    require(normal.in_flight());
    require(normal.end_up());
    require(!normal.in_flight());
    normal.cancel();
    require(!normal.pending());

    ClickDispatch nested;
    nested.queue();
    nested.begin_down();
    require(nested.in_flight());
    require(!nested.end_up());
    require(nested.pending());
    require(nested.end_down());

    ClickDispatch stale;
    stale.queue();
    require(!stale.end_up());
    require(!stale.pending());
    stale.queue();
    stale.begin_down();
    stale.cancel();
    require(!stale.end_down());
    require(!stale.end_up());

    ClickDispatch duplicate;
    duplicate.queue();
    duplicate.begin_down();
    require(!duplicate.end_down());
    require(duplicate.end_up());
    duplicate.cancel();
    require(!duplicate.end_up());
}
