#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace media {

struct Description {
    std::string codec;
    std::uint16_t data_reference = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t depth = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint32_t packet_frames = 1;
    std::uint32_t packet_bytes = 0;
    std::vector<std::uint8_t> bytes;
};

struct Sample {
    std::uint64_t offset;
    std::uint64_t time;
    std::uint32_t size;
    std::uint32_t duration;
    std::uint32_t description;
    bool keyframe;
};

struct Edit {
    std::uint64_t duration;
    std::int64_t media_time;
    std::int32_t rate;
};

struct Track {
    std::uint32_t id = 0;
    std::uint32_t flags = 0;
    std::uint32_t timescale = 0;
    std::uint64_t duration = 0;
    std::int16_t layer = 0;
    std::array<std::uint32_t, 11> placement{};
    std::string handler;
    std::vector<Description> descriptions;
    std::vector<Sample> samples;
    std::vector<Edit> edits;

    std::optional<std::size_t> sample_at(std::uint64_t movie_time, std::uint32_t movie_scale) const;
};

class Movie {
public:
    static Movie open(const std::filesystem::path& path);
    explicit Movie(std::vector<std::uint8_t> data);

    std::span<const std::uint8_t> packet(const Sample& sample) const;

    std::uint32_t timescale = 0;
    std::uint64_t duration = 0;
    std::vector<Track> tracks;

private:
    std::vector<std::uint8_t> data_;
};
}
