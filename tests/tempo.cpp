#include "tempo.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::int16_t> tone(unsigned rate, std::size_t frames, unsigned channels) {
    std::vector<std::int16_t> samples(frames * channels);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto phase = 2 * std::numbers::pi * 440 * static_cast<double>(i) / rate;
        const auto sample = static_cast<std::int16_t>(12000 * std::sin(phase));
        samples[i * channels] = sample;
        if (channels == 2) {
            samples[i * channels + 1] = -sample;
        }
    }
    return samples;
}

std::vector<std::int16_t> stretch(unsigned rate, unsigned channels,
                                  std::span<const std::int16_t> input, unsigned speed,
                                  bool packets = false) {
    media::Tempo tempo(rate, channels, speed);
    std::vector<std::int16_t> result;
    std::size_t packet = 0;
    while (!input.empty()) {
        const auto frames = input.size() / channels;
        const auto count = packets ? std::min(frames, (packet++ * 7919 % 2003) + 1) : frames;
        const auto output = tempo.push(input.first(count * channels));
        result.insert(result.end(), output.begin(), output.end());
        input = input.subspan(count * channels);
    }
    const auto tail = tempo.finish();
    result.insert(result.end(), tail.begin(), tail.end());
    require(tempo.finish().empty(), "Tempo finish was not idempotent");
    return result;
}

double magnitude(std::span<const std::int16_t> samples, unsigned rate, unsigned channels,
                 double frequency) {
    double real = 0;
    double imaginary = 0;
    const auto frames = samples.size() / channels;
    for (std::size_t i = 0; i < frames; ++i) {
        const auto phase = 2 * std::numbers::pi * frequency * static_cast<double>(i) / rate;
        const auto weight = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / (frames - 1));
        real += samples[i * channels] * weight * std::cos(phase);
        imaginary += samples[i * channels] * weight * std::sin(phase);
    }
    return std::hypot(real, imaginary);
}

void test_pitch_and_packets(unsigned rate, unsigned channels, unsigned speed) {
    const auto input = tone(rate, rate * speed + speed - 1, channels);
    const auto output = stretch(rate, channels, input, speed);
    require(output.size() == rate * channels,
            "Tempo output duration did not match the selected speed");
    require(output == stretch(rate, channels, input, speed, true),
            "Tempo waveform changed with arbitrary packet boundaries");
    const auto middle =
        std::span(output).subspan(rate / 10 * channels, (rate - rate / 5) * channels);
    const auto fundamental = magnitude(middle, rate, channels, 440);
    require(fundamental > magnitude(middle, rate, channels, 440 * speed) * 30,
            "Tempo shifted the tone to the accelerated pitch");
    double best = 0;
    unsigned dominant = 0;
    for (unsigned frequency = 430; frequency <= 450; ++frequency) {
        const auto amplitude = magnitude(middle, rate, channels, frequency);
        if (amplitude > best) {
            best = amplitude;
            dominant = frequency;
        }
    }
    require(dominant >= 439 && dominant <= 441, "Tempo tone dominant pitch changed");
    unsigned crossings = 0;
    for (std::size_t i = channels; i < middle.size(); i += channels) {
        crossings += middle[i - channels] <= 0 && middle[i] > 0;
    }
    const auto measured = static_cast<double>(crossings) * rate / (middle.size() / channels);
    require(measured > 438 && measured < 442, "Tempo tone zero crossing pitch changed");
    if (channels == 2) {
        for (std::size_t i = 0; i < output.size(); i += 2) {
            require(std::abs(output[i] + output[i + 1]) <= 1, "Tempo broke stereo phase coherence");
        }
    }
}

void test_short_and_tail(unsigned rate, unsigned channels, unsigned speed) {
    for (const auto frames : {0u, 1u, 2u, 3u, 4u, 5u, 7u, 17u, 255u, 1023u, 2049u, 8193u}) {
        auto input = tone(rate, frames, channels);
        if (frames >= 2) {
            for (unsigned c = 0; c < channels; ++c) {
                input[input.size() - channels + c] = c == 0 ? 16000 : -16000;
            }
        }
        const auto output = stretch(rate, channels, input, speed, true);
        require(output.size() == frames / speed * channels, "Short or odd tempo duration changed");
        if (!output.empty()) {
            require(output.back() == input.back(), "Tempo lost the final source sample");
            require(std::ranges::any_of(output, [](auto sample) { return sample != 0; }),
                    "Tempo replaced a short sound with silence");
        }
    }
    std::vector<std::int16_t> endpoint(rate * channels);
    for (std::size_t i = endpoint.size() - 128 * channels; i < endpoint.size(); i += channels) {
        endpoint[i] = 12000;
        if (channels == 2) {
            endpoint[i + 1] = -12000;
        }
    }
    const auto output = stretch(rate, channels, endpoint, speed);
    require(std::ranges::any_of(std::span(output).last(128 * channels),
                                [](auto sample) { return std::abs(sample) > 8000; }),
            "Tempo discarded a terminal sound after silence");
}

void test_lifecycle() {
    for (const auto rate : {0u, 7999u, 192001u}) {
        bool rejected = false;
        try {
            media::Tempo tempo(rate, 1);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Tempo accepted an invalid sample rate");
    }
    for (const auto channels : {0u, 3u}) {
        bool rejected = false;
        try {
            media::Tempo tempo(44100, channels);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Tempo accepted an invalid channel count");
    }
    for (const auto speed : {0u, 1u, 5u}) {
        bool rejected = false;
        try {
            media::Tempo tempo(44100, 2, speed);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Tempo accepted an unsupported speed");
    }
    media::Tempo tempo(44100, 2);
    const std::int16_t sample = 100;
    bool rejected = false;
    try {
        tempo.push(std::span(&sample, 1));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "Tempo accepted a partial stereo frame");
    require(tempo.push({}).empty(), "Empty tempo push produced audio");
    require(tempo.finish().empty(), "Empty tempo stream produced audio");
    rejected = false;
    try {
        tempo.push({});
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "Tempo accepted input after finish");
}

void test_latency(unsigned rate, unsigned speed) {
    media::Tempo tempo(rate, 2, speed);
    const auto input = tone(rate, rate, 2);
    std::size_t consumed = 0;
    while (consumed < input.size()) {
        const auto count = std::min<std::size_t>(128 * 2, input.size() - consumed);
        const auto output = tempo.push(std::span(input).subspan(consumed, count));
        consumed += count;
        if (!output.empty()) {
            const auto milliseconds = static_cast<double>(consumed / 2) * 1000 / rate;
            require(milliseconds <= (speed == 2 ? 300 : 200 * speed),
                    "Tempo startup exceeded the DVD prebuffer horizon");
            std::cout << "Tempo " << speed << "x " << rate << " Hz startup: " << milliseconds
                      << " ms\n";
            return;
        }
    }
    throw std::runtime_error("Tempo produced no streaming audio before EOF");
}

void test_stream_bounds(unsigned rate, unsigned speed) {
    for (const auto kind : {0, 1, 2}) {
        media::Tempo tempo(rate, 2, speed);
        std::uint32_t random = 123456789;
        std::vector<std::int16_t> input(256 * 2);
        std::size_t frames_in = 0;
        std::size_t frames_out = 0;
        std::size_t startup = 0;
        while (frames_in < rate * 5) {
            for (auto& sample : input) {
                random = random * 1664525 + 1013904223;
                sample = kind == 0   ? 0
                         : kind == 1 ? 12000
                                     : static_cast<std::int16_t>(random >> 16);
            }
            const auto count = std::min<std::size_t>(256, rate * 5 - frames_in);
            const auto output = tempo.push(std::span(input).first(count * 2));
            frames_in += count;
            frames_out += output.size() / 2;
            require(frames_out <= frames_in / speed,
                    "Streaming tempo exceeded its duration budget");
            if (!output.empty() && startup == 0) {
                startup = frames_in;
            }
            if (kind == 0) {
                require(std::ranges::all_of(output, [](auto sample) { return sample == 0; }),
                        "Tempo introduced audio into silence");
            }
        }
        require(startup != 0 && startup * 1000 / rate < (speed == 2 ? 300 : 200 * speed),
                "Silence, DC or random input exceeded the startup horizon");
        if (rate == 48000) {
            std::cout << "Tempo " << speed << "x 48000 Hz kind " << kind
                      << " startup: " << static_cast<double>(startup) * 1000 / rate << " ms\n";
        }
        const auto tail = tempo.finish();
        require((frames_out + tail.size() / 2) == frames_in / speed,
                "Long streaming tempo accumulated a duration error");
        require(tail.size() / 2 < rate / 4, "Tempo retained more than a bounded final tail");
        if (kind == 0) {
            require(std::ranges::all_of(tail, [](auto sample) { return sample == 0; }),
                    "Tempo introduced a nonzero tail into silence");
        }
    }
}

}

int main() {
    try {
        for (const auto speed : {2u, 3u, 4u}) {
            for (const auto rate : {22050u, 44100u, 48000u}) {
                for (const auto channels : {1u, 2u}) {
                    test_pitch_and_packets(rate, channels, speed);
                    test_short_and_tail(rate, channels, speed);
                }
                test_latency(rate, speed);
                test_stream_bounds(rate, speed);
            }
            for (const auto rate : {8000u, 11025u, 96000u, 192000u}) {
                test_short_and_tail(rate, 2, speed);
                test_stream_bounds(rate, speed);
            }
        }
        test_lifecycle();
        std::cout
            << "2x/3x/4x tempo duration, pitch, channels, streaming and lifecycle checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
