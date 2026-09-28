#include "media/movie.h"

#include <cstddef>
#include <stdexcept>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        const media::Movie movie({data, data + size});
        for (const auto& track : movie.tracks) {
            for (const auto& sample : track.samples) {
                movie.packet(sample);
            }
            for (const auto time : {std::uint64_t(0), movie.duration / 2, movie.duration}) {
                track.sample_at(time, movie.timescale);
            }
        }
    } catch (const std::runtime_error&) {
    }
    return 0;
}
