#include "video.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void apple_video() {
    media::Description format;
    format.codec = "rpza";
    format.width = format.height = 4;
    std::vector<std::uint8_t> packet{0xe1, 0, 0, 7, 0xa0, 0x03, 0xe0};
    media::Video decoder;
    const auto green = decoder.image(format, packet).pixels;
    if (green.size() != 64 || green[0] != 0 || green[1] < 248 || green[2] != 0) {
        throw std::runtime_error("Apple Video solid block decoded incorrectly");
    }
    packet.insert(packet.end(), {0xe1, 0, 0, 7, 0xa0, 0x7c, 0});
    if (decoder.image(format, packet).pixels != green) {
        throw std::runtime_error("Apple Video ignored the declared frame boundary");
    }
    packet[3] = 30;
    try {
        decoder.image(format, packet);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("Truncated Apple Video frame accepted");
}

}

int wmain(int argc, wchar_t** argv) {
    if (argc != 1 && argc != 3) {
        std::cerr << "Usage: video-test <movie> <bgra-output>\n";
        return 1;
    }
    try {
        apple_video();
        if (argc == 1) {
            return 0;
        }
        const auto movie = media::Movie::open(argv[1]);
        for (const auto& track : movie.tracks) {
            if (track.handler != "vide") {
                continue;
            }
            media::Video decoder;
            std::ofstream output(argv[2], std::ios::binary);
            std::vector<std::uint8_t> first;
            for (std::size_t i = 0; i < track.samples.size(); ++i) {
                const auto& frame = decoder.decode(movie, track, i);
                if (!i) {
                    first = frame.pixels;
                }
                output.write(reinterpret_cast<const char*>(frame.pixels.data()),
                             frame.pixels.size());
            }
            if (decoder.decode(movie, track, 0).pixels != first) {
                throw std::runtime_error("Backward seek did not reproduce the first frame");
            }
            if (!output) {
                throw std::runtime_error("Cannot write decoded frames");
            }
            std::cout << track.samples.size()
                      << " frames decoded; backward seek reproduced the first frame.\n";
            return 0;
        }
        throw std::runtime_error("Movie has no video track");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
