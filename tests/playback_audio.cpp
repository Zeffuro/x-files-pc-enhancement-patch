#include "playback/audio.h"
#include "media/tempo.h"

#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
std::vector<std::int16_t> submitted;
unsigned ratio = 0, starts = 0, outputs = 0;
bool current_device = true;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

using Bytes = std::vector<std::uint8_t>;

void word(Bytes& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

Bytes words(std::initializer_list<std::uint32_t> values) {
    Bytes bytes;
    for (auto value : values) {
        word(bytes, value);
    }
    return bytes;
}

void atom(Bytes& bytes, const char* name, const Bytes& content) {
    word(bytes, static_cast<std::uint32_t>(content.size() + 8));
    bytes.insert(bytes.end(), name, name + 4);
    bytes.insert(bytes.end(), content.begin(), content.end());
}

media::Movie fixture(unsigned channels, unsigned rate) {
    Bytes bytes, pcm;
    const auto frames = rate * 2 + 1;
    for (unsigned frame = 0; frame < frames; ++frame) {
        const auto sample =
            static_cast<std::int16_t>(12000 * std::sin(frame * 440.0 * 6.283185307 / rate));
        for (unsigned channel = 0; channel < channels; ++channel) {
            const auto value = static_cast<std::uint16_t>(channel ? -sample : sample);
            pcm.push_back(static_cast<std::uint8_t>(value >> 8));
            pcm.push_back(static_cast<std::uint8_t>(value));
        }
    }
    atom(bytes, "mdat", pcm);
    Bytes description(28);
    description[7] = 1;
    description[17] = static_cast<std::uint8_t>(channels);
    description[19] = 16;
    description[24] = static_cast<std::uint8_t>(rate >> 8);
    description[25] = static_cast<std::uint8_t>(rate);
    auto descriptions = words({0, 1});
    atom(descriptions, "twos", description);
    Bytes table, info, media, track, body;
    atom(table, "stsd", descriptions);
    atom(table, "stco", words({0, 1, 8}));
    atom(table, "stsc", words({0, 1, 1, frames, 1}));
    atom(table, "stsz", words({0, 1, frames}));
    atom(table, "stts", words({0, 1, frames, 1}));
    atom(info, "stbl", table);
    atom(media, "mdhd", words({0, 0, 0, rate, frames}));
    atom(media, "hdlr", words({0, 0, 0x736f756e}));
    atom(media, "minf", info);
    atom(track, "tkhd", words({1, 0, 0, 1, 0, frames}));
    atom(track, "mdia", media);
    atom(body, "mvhd", words({0, 0, 0, rate, frames}));
    atom(body, "trak", track);
    atom(bytes, "moov", body);
    return media::Movie(std::move(bytes));
}

void verify(unsigned channels, unsigned rate) {
    const auto movie = fixture(channels, rate);
    playback::Audio audio(movie, movie.tracks.at(0));
    audio.play(0, rate, 256);
    const auto normal = submitted;
    require(normal.size() == (rate * 2 + 1) * 2 && ratio == 1,
            "Normal PCM length or playback rate changed");
    for (unsigned speed : {2, 3, 4, 2}) {
        media::Tempo reference(rate, 2, speed);
        auto expected = reference.push(normal);
        const auto tail = reference.finish();
        expected.insert(expected.end(), tail.begin(), tail.end());
        audio.prepare(speed);
        audio.play(0, rate, 256, speed);
        require(submitted == expected && ratio == 1,
                "Fast output did not use natural-pitch PCM at its original sample rate");
        const auto cached_outputs = outputs;
        audio.prepare(speed);
        audio.play(0, rate, 256, speed);
        require(outputs == cached_outputs && submitted == expected,
                "Preparing the same speed discarded the cached PCM or voice");
        audio.play(1001, 1000, 128, speed);
        const auto offset = static_cast<std::size_t>(1001ull * rate / 1000 / speed * 2);
        require(submitted == std::vector<std::int16_t>(expected.begin() + offset, expected.end()),
                "Fast seek did not rebase on a stereo source frame");
        current_device = false;
        const auto prior_outputs = outputs;
        audio.refresh(1, 2);
        require(outputs == prior_outputs + 1 && ratio == 1 &&
                    submitted.size() == expected.size() - rate / 2 / speed * 2,
                "Audio device replacement lost natural pitch or source position");
        audio.play(1, 2, 256, 1);
        require(ratio == 1 && submitted == std::vector<std::int16_t>(normal.begin() + rate / 2 * 2,
                                                                     normal.end()),
                "Releasing fast playback retained stretched PCM");
        const auto prior_starts = starts;
        audio.play(rate * 3, rate, 256, speed);
        audio.play(0, 0, 256, speed);
        require(starts == prior_starts && submitted.empty(),
                "End or invalid scale replayed stale PCM");
    }
    for (const auto speed : {0u, 5u}) {
        bool rejected = false;
        try {
            audio.prepare(speed);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Audio preparation accepted an unsupported speed");
    }
    audio.stop();
}

void export_pcm(const std::filesystem::path& path, unsigned rate) {
    std::ofstream file(path, std::ios::binary);
    const auto integer = [&](std::uint32_t value, unsigned bytes) {
        for (unsigned byte = 0; byte < bytes; ++byte) {
            file.put(static_cast<char>(value >> (byte * 8)));
        }
    };
    const auto bytes = static_cast<std::uint32_t>(submitted.size() * 2);
    file.write("RIFF", 4);
    integer(bytes + 36, 4);
    file.write("WAVEfmt ", 8);
    integer(16, 4);
    integer(1, 2);
    integer(2, 2);
    integer(rate, 4);
    integer(rate * 4, 4);
    integer(4, 2);
    integer(16, 2);
    file.write("data", 4);
    integer(bytes, 4);
    file.write(reinterpret_cast<const char*>(submitted.data()), bytes);
    require(static_cast<bool>(file), "Cannot export captured movie PCM");
}

void verify_real_movie(const std::filesystem::path& source, const std::filesystem::path& prefix) {
    const auto movie = media::Movie::open(source);
    for (const auto& track : movie.tracks) {
        if (track.handler != "soun") {
            continue;
        }
        playback::Audio audio(movie, track);
        const auto rate = track.descriptions.at(0).sample_rate;
        audio.play(0, movie.timescale, 256);
        const auto samples = submitted.size();
        export_pcm(prefix.wstring() + L"-normal.wav", rate);
        for (const auto speed : {2u, 3u, 4u}) {
            const auto start = std::chrono::steady_clock::now();
            audio.play(0, movie.timescale, 256, speed);
            const auto elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            require(submitted.size() == samples / 2 / speed * 2 && ratio == 1,
                    "Actual movie did not retain its sample rate at the selected duration");
            export_pcm(prefix.wstring() + L"-" + std::to_wstring(speed) + L"x.wav", rate);
            std::cout << "Captured " << source.string() << ": " << rate << " Hz, " << samples / 2
                      << " -> " << submitted.size() / 2 << " frames at " << speed
                      << "x, preparation " << elapsed << " seconds\n";
        }
        return;
    }
    throw std::runtime_error("Actual movie has no sound track");
}
}

namespace playback {
struct Output::State {};

Output::Output(const WAVEFORMATEX&) {
    ++outputs;
    ::current_device = true;
}

Output::~Output() = default;

bool Output::current_device() const {
    return ::current_device;
}

void Output::play(std::span<const std::int16_t> samples) {
    submitted.assign(samples.begin(), samples.end());
    ++starts;
}

void Output::stop() {
    submitted.clear();
}

void Output::volume(std::int16_t, std::int16_t) {}

void Output::speed(unsigned multiplier) {
    ratio = multiplier;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        for (unsigned rate : {22050, 44100, 48000}) {
            for (unsigned channels : {1, 2}) {
                verify(channels, rate);
            }
        }
        if (argc == 3) {
            verify_real_movie(argv[1], argv[2]);
        }
        std::cout << "QuickTime natural pitch, seek, release and device rebasing passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
