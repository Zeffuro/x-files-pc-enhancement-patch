#include "input.h"
#include "playback/fast_forward_input.h"

namespace dvd {
bool poll_speed(playback::HeldFastForward& state, HWND owner, unsigned key, bool gamepad,
                const controller::Profile& profile) {
    return playback::poll_fast_forward(state, owner, key, gamepad, true, profile);
}
}
