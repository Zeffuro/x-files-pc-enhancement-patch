#include "rumble.h"
#include "rumble_state.h"

#include <xinput.h>

namespace enhancements {
namespace {

class XInputOutput final : public rumble::Output {
public:
    bool set(unsigned player, rumble::Motors motors) noexcept override {
        XINPUT_VIBRATION value{motors.low, motors.high};
        return XInputSetState(player, &value) == ERROR_SUCCESS;
    }
};

SRWLOCK lock = SRWLOCK_INIT;
rumble::State state;
XInputOutput output;
HWND owner = nullptr;
HANDLE timer = nullptr;

struct Guard {
    Guard() noexcept {
        AcquireSRWLockExclusive(&lock);
    }

    ~Guard() {
        ReleaseSRWLockExclusive(&lock);
    }
};

bool foreground() noexcept {
    GUITHREADINFO info{sizeof(GUITHREADINFO)};
    return owner && GetGUIThreadInfo(0, &info) && info.hwndActive == owner;
}

void CALLBACK tick(void*, BOOLEAN) noexcept {
    Guard guard;
    if (!foreground()) {
        state.stop(output);
    } else {
        state.tick(output, GetTickCount64());
    }
}

}

void attach_rumble(HWND window) noexcept {
    Guard guard;
    if (!timer) {
        owner = window;
        if (!CreateTimerQueueTimer(&timer, nullptr, tick, nullptr, 10, 10, WT_EXECUTEDEFAULT)) {
            timer = nullptr;
            owner = nullptr;
        }
    }
}

void update_rumble(unsigned player, bool allowed) noexcept {
    Guard guard;
    state.update(output, player, allowed && timer && foreground(), GetTickCount64());
}

void play_rumble(rumble::Effect effect, std::uintptr_t source) noexcept {
    if (!effect.duration_ms) {
        return;
    }
    Guard guard;
    if (timer && foreground()) {
        state.play(output, effect, source, GetTickCount64());
    } else {
        state.stop(output);
    }
}

void stop_rumble() noexcept {
    Guard guard;
    state.stop(output);
}

void cancel_rumble(std::uintptr_t source) noexcept {
    Guard guard;
    state.cancel(output, source);
}

void detach_rumble() noexcept {
    HANDLE previous = nullptr;
    {
        Guard guard;
        state.stop(output);
        owner = nullptr;
        previous = timer;
        timer = nullptr;
    }
    if (previous) {
        // Wait outside the lock so an already queued callback can finish.
        if (!DeleteTimerQueueTimer(nullptr, previous, INVALID_HANDLE_VALUE)) {
            Guard guard;
            timer = previous;
        }
    }
}

}
