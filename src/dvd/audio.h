#pragma once

#include <cstdint>
#include <memory>

struct AVFrame;

namespace dvd {
class Audio {
public:
    Audio();
    ~Audio();
    void push(const AVFrame& frame, int first, int count);
    void finish();
    bool drained();
    std::int64_t played() const;
    void pause(bool paused);
    void speed(unsigned multiplier);
    void clear();

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
