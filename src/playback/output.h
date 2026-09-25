#pragma once

#include <windows.h>
#include <mmreg.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace playback {

struct OutputDevice {
    std::wstring id;
    std::wstring name;
};

std::vector<OutputDevice> output_devices();

class Output {
public:
    explicit Output(const WAVEFORMATEX& format);
    ~Output();
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    bool current_device() const;
    void play(std::span<const std::int16_t> samples);
    void stop();
    void volume(std::int16_t level, std::int16_t balance);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
