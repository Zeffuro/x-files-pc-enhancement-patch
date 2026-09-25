#include "compressed_audio.h"
#include "movie.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void invalid_descriptions() {
    for (const auto* codec : {"QDMC", "QDM2", "xxxx"}) {
        for (const auto size : {0u, 43u, 44u, 60u}) {
            media::Description description;
            description.codec = codec;
            description.channels = 2;
            description.sample_rate = 44100;
            description.bytes.resize(size);
            bool rejected = false;
            try {
                media::CompressedAudio decoder(description);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            require(rejected, "Accepted an invalid audio description");
        }
    }
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
        media::CompressedAudio decoder(description);
        std::uint64_t frames = 0;
        for (const auto& sample : track.samples) {
            require(sample.time == frames, "Audio packet timeline is discontinuous");
            const auto pcm = decoder.decode(movie.packet(sample));
            require(pcm.size() == static_cast<std::size_t>(sample.duration) * description.channels,
                    "Decoded duration differs from the sample table");
            output.write(reinterpret_cast<const char*>(pcm.data()), pcm.size() * sizeof(pcm[0]));
            frames += sample.duration;
        }
        require(static_cast<bool>(output), "PCM write failed");
        std::cout << description.codec << ": " << frames << " decoded frames\n";
        return;
    }
    throw std::runtime_error("No audio track found");
}

}

int wmain(int argc, wchar_t** argv) {
    try {
        invalid_descriptions();
        if (argc == 3) {
            decode_file(argv[1], argv[2]);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
