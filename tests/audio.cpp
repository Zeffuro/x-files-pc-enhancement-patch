#include "ima4.h"
#include "movie.h"
#include "pcm.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_packets() {
    media::Ima4 mono(1);
    std::vector<std::uint8_t> packet(media::Ima4::packet_bytes);
    auto silence = mono.decode(packet);
    require(silence.size() == 64 && std::ranges::all_of(silence, [](auto x) { return x == 0; }),
            "Silent packet did not decode to silence");
    packet[2] = 0x71;
    const auto signal = mono.decode(packet);
    require(signal[0] == 1 && signal[1] == 12 && signal[2] == 14,
            "Nibble order or IMA rounding is incorrect");
    packet[1] = 89;
    bool rejected = false;
    try {
        mono.decode(packet);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "Invalid step index accepted");
    media::Ima4 stereo(2);
    packet.assign(68, 0);
    packet[34] = 0xff;
    packet[35] = 0x80;
    const auto channels = stereo.decode(packet);
    for (unsigned frame = 0; frame < 64; ++frame) {
        require(channels[frame * 2] == 0 && channels[frame * 2 + 1] == -128,
                "Stereo channel layout is incorrect");
    }
}

void test_pcm() {
    const std::vector<std::uint8_t> signed8{0, 127, 128, 255};
    require(media::decode_signed_pcm(signed8, 8) ==
                std::vector<std::int16_t>{0, 32512, -32768, -256},
            "Signed 8-bit PCM was treated as unsigned or lost its amplitude");
    const std::vector<std::uint8_t> signed16{0, 0, 127, 255, 128, 0, 255, 255};
    require(media::decode_signed_pcm(signed16, 16) ==
                std::vector<std::int16_t>{0, 32767, -32768, -1},
            "Signed 16-bit PCM lost big-endian ordering");
    bool rejected = false;
    try {
        media::decode_signed_pcm(std::span(signed16).first(7), 16);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "Truncated 16-bit PCM sample was accepted");
}

void decode_file(const std::filesystem::path& source, const std::filesystem::path& destination) {
    const auto movie = media::Movie::open(source);
    std::ofstream output(destination, std::ios::binary);
    require(static_cast<bool>(output), "Cannot create PCM file");
    for (const auto& track : movie.tracks) {
        if (track.handler != "soun") {
            continue;
        }
        const auto& description = track.descriptions.at(0);
        media::Ima4 decoder(description.channels);
        for (const auto& sample : track.samples) {
            require(sample.duration == description.packet_frames &&
                        sample.size == description.packet_bytes,
                    "Audio packet indexing is incorrect");
            const auto pcm = description.codec == "twos"
                                 ? media::decode_signed_pcm(movie.packet(sample), description.depth)
                                 : decoder.decode(movie.packet(sample));
            output.write(reinterpret_cast<const char*>(pcm.data()), pcm.size() * sizeof(pcm[0]));
        }
        require(static_cast<bool>(output), "PCM write failed");
        return;
    }
    throw std::runtime_error("No audio track found");
}

}

int wmain(int argc, wchar_t** argv) {
    try {
        test_packets();
        test_pcm();
        if (argc == 3) {
            decode_file(argv[1], argv[2]);
        }
        std::cout << "IMA4 and signed PCM decoding checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
