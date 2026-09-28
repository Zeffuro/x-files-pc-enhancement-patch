#include "dvd/clock.h"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
}

int main() {
    try {
        dvd::Clock clock;
        clock.reset(900000, 1000);
        require(clock.advance(1100, 9000, 1, false, false) == 909000,
                "Normal DVD clock ignored its seek origin");
        clock.anchor(5100);
        require(clock.advance(5200, 18000, 1, false, false) == 918000,
                "Paused wall time advanced the DVD audio clock");
        clock.reset(clock.position(), 5200);
        require(clock.advance(5700, 80000, 2, true, false) == 1008000,
                "Muted DVD clock did not double wall time independently of stale audio");
        clock.anchor(9700);
        require(clock.advance(9800, 0, 2, true, false) == 1026000,
                "Reset timer included paused wall time in acceleration");
        clock.reset(clock.position(), 9800);
        require(clock.advance(9900, 9000, 1, false, false) == 1035000,
                "Audio resume did not rebase at the accelerated media position");
        require(clock.advance(10000, 18000, 1, false, true) == 1044000,
                "Final video tail did not retain normal clock rate");
        bool rejected = false;
        try {
            clock.advance(9999, 18000, 1, false, false);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Backwards wall clock accepted");
        clock.reset(INT64_MAX - 10, 0);
        rejected = false;
        try {
            clock.advance(1, 0, 2, true, false);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Accelerated clock overflow accepted");
        for (unsigned speed = 2; speed <= 4; ++speed) {
            clock.reset(90000, 0);
            require(clock.advance(100, 999999, speed, true, false) == 90000 + 9000 * speed,
                    "Muted clock ignored the configured speed");
            clock.reset(90000, 0);
            require(clock.advance(100, 8500 * speed, speed, false, false) == 90000 + 8500 * speed,
                    "Audible speed did not follow consumed source samples");
            require(clock.advance(200, 8500 * speed, speed, false, true) == 90000 + 17500 * speed,
                    "Accelerated audio tail did not retain its speed");
        }
        std::cout << "DVD origin, pause, 2x/3x/4x, audible/muted and tail clocks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
