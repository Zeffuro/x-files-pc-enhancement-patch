#pragma once

#include "media/video.h"
#include "media/frame_reference.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace playback {
struct MovieSnapshot {
    std::uint64_t id;
    std::filesystem::path path;
    std::int32_t time;
    std::uint64_t duration;
    std::uint32_t timescale;
    bool active, playing, video, audio, override_loaded;
    std::wstring caption;
    media::Frame preview;
    std::uint64_t last_draw = 0;
    int left = 0, top = 0, width = 0, height = 0;
    std::optional<media::FrameReference> image;
};

// Call on the movie thread. Copies state without advancing clocks or delivering callbacks.
std::vector<MovieSnapshot> inspect_movies();
std::uint64_t inspect_time(std::uint64_t id);
}
