#include "enhancements/rumble_state.h"
#include "enhancements/rumble_catalog.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace enhancements::rumble;
constexpr std::uintptr_t gun = 1, clip = 2;
constexpr int forward = 1 << 16;

void require(bool value) {
    if (!value) {
        throw std::runtime_error("rumble state regression");
    }
}

struct FakeOutput final : Output {
    struct Write {
        unsigned player;
        Motors motors;
    };

    std::array<Motors, device_count> motors{};
    std::array<bool, device_count> connected{true, true, true, true};
    std::vector<Write> writes;

    bool set(unsigned player, Motors value) noexcept override {
        writes.push_back({player, value});
        if (!connected[player]) {
            return false;
        }
        motors[player] = value;
        return true;
    }
};

void catalog_and_starts() {
    const auto short_cue = short_effect();
    require(short_cue.motors == Motors{44975, 65535} && short_cue.duration_ms == 83);
    const auto long_cue = clip_effect(L"XV\\21782.XMV");
    require(long_cue.motors == Motors{64250, 65535} && long_cue.duration_ms == 1000);
    for (const auto id : {21230, 21232, 21234, 21238, 21662, 21664, 21716, 21718, 21784, 21808,
                          21812, 21818, 28456, 44483, 49014, 49022}) {
        const auto path = L"xv/" + std::to_wstring(id) + L".xmv";
        const auto effect = start_effect(path, 0, 0, forward);
        require(effect.motors == short_cue.motors && effect.duration_ms == 83);
    }
    for (const auto path : {L"XS\\92148.AMV", L"xv/21782.xmv", L"xv/49022.xmv"}) {
        require(start_effect(path, 0, 0, forward).duration_ms != 0);
        require(!start_effect(path, 1, 0, forward).duration_ms);
        require(!start_effect(path, 0, forward, forward).duration_ms);
        require(!start_effect(path, 0, 0, 0).duration_ms);
        require(!start_effect(path, 0, 0, -forward).duration_ms);
    }
    // Negative branches use different movies, or never start the helper sound.
    for (const auto path :
         {L"xv/49013.xmv", L"xv/23434.xmv", L"xv/23430.xmv", L"xv/19656.xmv", L"xs/other.amv",
          L"21782.xmv", L"xn/21782.xmv", L"xv/21782.xmv.bak", L"../xv/21782.xmv",
          L"copy/xs/92148.amv", L"vob/21782.vob", L"xs/21782.xmv", L"xv/92148.amv"}) {
        require(!start_effect(path, 0, 0, forward).duration_ms);
    }
}

void pulse_and_expiration() {
    FakeOutput output;
    State state;
    const auto effect = short_effect();
    state.play(output, effect, gun, 1000);
    require(output.writes.empty());
    state.update(output, 1, true, 1000);
    state.play(output, effect, gun, 1000);
    require(output.motors[1] == effect.motors);
    state.tick(output, 1082);
    require(output.writes.size() == 1);
    state.tick(output, 1083);
    require(output.motors[1] == Motors{});
    state.play(output, effect, gun, 1090);
    state.tick(output, 1100);
    require(output.motors[1] == Motors{});
    state.play(output, effect, gun, 1101);
    require(output.motors[1] == Motors{});
}

void playback_lifecycle() {
    for (const auto path : {L"xs/92148.amv", L"xv/21782.xmv"}) {
        PlaybackCue playback;
        FakeOutput output;
        State state;
        // Consume an initial start even when no controller owns output yet.
        state.play(output, playback.start(path, 0, 0, forward), clip, 1000);
        state.update(output, 0, true, 1010);
        state.play(output, playback.start(path, 0, 0, forward), clip, 1010);
        require(output.writes.empty());
        playback.seek(0);
        const auto cue = playback.start(path, 0, 0, forward);
        require(cue.duration_ms != 0);
        state.play(output, cue, clip, 1020);
        require(output.motors[0].high != 0);
        require(!playback.start(path, 0, forward, forward).duration_ms);
        state.cancel(output, clip);
        require(!playback.start(path, 0, 0, forward).duration_ms);
        playback.seek(100);
        require(!playback.start(path, 100, 0, forward).duration_ms);
        playback.seek(0);
        require(!playback.start(path, 0, forward, forward).duration_ms);
        require(playback.start(path, 0, 0, forward).duration_ms != 0);
    }
}

void long_cue_and_overlap() {
    FakeOutput output;
    State state;
    const auto long_cue = clip_effect(L"xv/21782.xmv");
    const auto short_cue = short_effect();
    state.update(output, 0, true, 1000);
    state.play(output, long_cue, clip, 1000);
    for (unsigned now = 1016; now < 2000; now += 16) {
        state.update(output, 0, true, now);
        require(output.motors[0] == long_cue.motors);
    }
    state.tick(output, 2000);
    require(output.motors[0] == Motors{});
    state.update(output, 0, true, 3000);
    state.play(output, long_cue, clip, 3000);
    state.play(output, short_cue, gun, 3020);
    require(output.motors[0] == short_cue.motors);
    state.cancel(output, clip);
    require(output.motors[0] == short_cue.motors);
    for (unsigned now = 3040; now < 4000; now += 40) {
        state.update(output, 0, true, now);
        require(output.motors[0] == short_cue.motors);
    }
    state.tick(output, 4000);
    require(output.motors[0] == Motors{});
    state.update(output, 0, true, 5000);
    state.play(output, short_cue, gun, 5000);
    state.play(output, long_cue, clip, 5020);
    require(output.motors[0] == long_cue.motors);
    state.cancel(output, gun);
    require(output.motors[0] == long_cue.motors);
    state.cancel(output, clip);
    require(output.motors[0] == Motors{});
}

void native_stop_rewind_play() {
    FakeOutput output;
    State state;
    state.update(output, 0, true, 1000);
    state.play(output, short_effect(), gun, 1000);
    state.cancel(output, gun);
    require(output.motors[0] == Motors{});
    state.cancel(output, gun);
    state.play(output, short_effect(), gun, 1010);
    require(output.motors[0].high != 0);
    state.cancel(output, gun);
    state.tick(output, 1020);
    require(output.motors[0] == Motors{});
    state.stop(output);
    state.play(output, short_effect(), gun, 1030);
    require(output.motors[0] == Motors{});
}

void cancellation_and_transfer() {
    for (unsigned reason = 0; reason < 4; ++reason) {
        FakeOutput output;
        State state;
        state.update(output, 0, true, 100);
        state.play(output, short_effect(), gun, 100);
        if (reason == 0) {
            state.update(output, 0, false, 101);
        } else if (reason == 1) {
            state.update(output, no_device, true, 101);
        } else if (reason == 2) {
            state.update(output, 2, true, 101);
        } else {
            state.stop(output);
        }
        require(output.motors[0] == Motors{} && output.motors[2] == Motors{});
        state.tick(output, 102);
        require(output.motors[0] == Motors{} && output.motors[2] == Motors{});
        if (reason == 2) {
            state.play(output, short_effect(), gun, 103);
            require(output.motors[2].high != 0 && output.motors[0] == Motors{});
        }
    }
}

void reconnect_and_failure() {
    FakeOutput output;
    State state;
    state.update(output, 0, true, 100);
    state.play(output, short_effect(), gun, 100);
    output.connected[0] = false;
    state.update(output, 1, true, 101);
    require(output.writes.back().player == 0 && output.writes.back().motors == Motors{});
    output.connected[0] = true;
    state.tick(output, 102);
    require(output.motors[0] == Motors{});
    output.connected[1] = false;
    state.play(output, short_effect(), gun, 103);
    output.connected[1] = true;
    state.tick(output, 104);
    require(output.motors[1] == Motors{});
    state.update(output, 1, true, 105);
    require(output.motors[1] == Motors{});
}

void restart_and_stall() {
    FakeOutput output;
    State state;
    state.update(output, 3, true, 100);
    state.cancel(output);
    state.play(output, short_effect(), gun, 100);
    state.play(output, short_effect(), gun, 150);
    state.tick(output, 199);
    require(output.motors[3].low != 0);
    state.tick(output, 200);
    require(output.motors[3] == Motors{});
    state.update(output, 3, true, 500);
    require(output.motors[3] == Motors{});
    state.play(output, short_effect(), gun, 501);
    state.update(output, 3, true, 1000);
    require(output.motors[3] == Motors{});
}
}

int main() {
    catalog_and_starts();
    playback_lifecycle();
    native_stop_rewind_play();
    pulse_and_expiration();
    long_cue_and_overlap();
    cancellation_and_transfer();
    reconnect_and_failure();
    restart_and_stall();
}
