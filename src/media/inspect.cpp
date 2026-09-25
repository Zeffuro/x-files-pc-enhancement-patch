#include "movie.h"

#include <iostream>
#include <map>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "Usage: xfiles-media <movie-file>\n";
        return 1;
    }
    try {
        const auto movie = media::Movie::open(argv[1]);
        std::cout << "movie_timescale=" << movie.timescale << '\n';
        for (const auto& track : movie.tracks) {
            std::cout << "track=" << track.id << " handler=" << track.handler
                      << " timescale=" << track.timescale << " edits=" << track.edits.size();
            if (track.handler != "vide" && track.handler != "soun" && track.handler != "text") {
                std::cout << " indexing=pending\n";
                continue;
            }
            std::map<std::string, unsigned> counts;
            unsigned changes = 0;
            std::string last;
            for (const auto& sample : track.samples) {
                const auto& desc = track.descriptions[sample.description];
                const auto packet = movie.packet(sample);
                if (desc.codec == "jpeg" &&
                    (packet.size() < 2 || packet[0] != 0xff || packet[1] != 0xd8)) {
                    throw std::runtime_error("JPEG sample lacks SOI marker.");
                }
                if (!last.empty() && last != desc.codec) {
                    ++changes;
                }
                last = desc.codec;
                ++counts[desc.codec];
            }
            std::cout << " samples=" << track.samples.size() << " codec_changes=" << changes;
            for (const auto& [codec, count] : counts) {
                std::cout << ' ' << codec << '=' << count;
            }
            std::cout << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
