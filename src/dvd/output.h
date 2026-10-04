#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "settings.h"
#include <windows.h>

struct AVFrame;

namespace dvd {

class Output {
public:
    explicit Output(HWND parent);
    ~Output();
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    void rectangles(RECT source, RECT dest, RECT client);
    void video(const AVFrame& frame);
    void caption(std::wstring text, const CaptionStyle& style);
    void audio(const AVFrame& frame, int first_sample, int sample_count);
    void finish_audio();
    bool drained();
    std::int64_t played() const;
    void pause(bool paused);
    void speed(unsigned multiplier);
    void clear();
    void discard_audio();
    void show(bool visible);
    HWND window() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
