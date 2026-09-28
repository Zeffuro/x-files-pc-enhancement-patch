#include "dvd/output.h"
#include "dvd/input.h"

#include <algorithm>
extern "C" {
#include <libavutil/frame.h>
}

namespace dvd {
bool poll_speed(playback::HeldFastForward& state, HWND, unsigned key, bool) {
    const bool down = GetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr, 0) != 0;
    const bool focus = GetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_UNFOCUSED", nullptr, 0) == 0;
    return state.update(down, key && focus);
}

struct Output::State {
    HWND window = nullptr;
    bool paused = false;
    unsigned speed = 1;
    bool underrun = GetEnvironmentVariableW(L"XFILES_DVD_TEST_UNDERRUN", nullptr, 0) != 0;
    std::int64_t samples = 0;
    std::int64_t elapsed = 0;
    ULONGLONG start = GetTickCount64();

    std::int64_t time() {
        const auto now = GetTickCount64();
        if (!paused) {
            elapsed = std::min(elapsed + static_cast<std::int64_t>(now - start) * 90 * speed,
                               samples * 90000 / 48000);
        }
        start = now;
        return elapsed;
    }
};

Output::Output(HWND parent) : state_(std::make_unique<State>()) {
    state_->window = parent;
}

Output::~Output() = default;

void Output::rectangles(RECT, RECT, RECT) {}

void Output::video(const AVFrame&) {}

void Output::caption(std::wstring text, const CaptionStyle&) {
    SetWindowTextW(state_->window, text.c_str());
}

void Output::audio(const AVFrame&, int, int count) {
    state_->time();
    state_->samples += count;
    SetPropW(state_->window, L"XFilesDvdTestAudio", reinterpret_cast<HANDLE>(1));
}

bool Output::drained() {
    if (GetEnvironmentVariableW(L"XFILES_DVD_TEST_DRAIN_PENDING", nullptr, 0)) {
        return false;
    }
    return state_->underrun || played() >= state_->samples * 90000 / 48000;
}

void Output::pause(bool value) {
    state_->elapsed = state_->time();
    state_->start = GetTickCount64();
    state_->paused = value;
}

void Output::speed(unsigned multiplier) {
    state_->time();
    state_->speed = multiplier;
    SetPropW(state_->window, L"XFilesDvdTestSpeed", reinterpret_cast<HANDLE>(multiplier));
}

void Output::clear() {
    discard_audio();
    SetWindowTextW(state_->window, L"");
}

void Output::discard_audio() {
    state_->samples = state_->elapsed = 0;
    state_->start = GetTickCount64();
    RemovePropW(state_->window, L"XFilesDvdTestAudio");
}

void Output::show(bool) {}

HWND Output::window() const {
    return state_->window;
}

std::int64_t Output::played() const {
    if (state_->underrun) {
        return 0;
    }
    return std::min(state_->time(), state_->samples * 90000 / 48000);
}
}
