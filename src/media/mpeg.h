#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

extern "C" {
#include <libavutil/rational.h>
}

struct AVFrame;

namespace media {

enum class MpegStream { video, audio };

struct MpegInfo {
    int width = 0;
    int height = 0;
    AVRational frame_rate{};
    AVRational sample_aspect_ratio{};
    int field_order = 0;
    int sample_rate = 0;
    int channels = 0;
    std::int64_t origin = 0;
    std::int64_t video_start = 0;
    std::int64_t audio_start = 0;
};

struct MpegFrame {
    MpegStream stream;
    const AVFrame* frame;
    std::int64_t time;
    std::int64_t duration;
    bool estimated_time;
};

// Shared A/V timeline in 90 kHz ticks.
class MpegSource {
public:
    explicit MpegSource(const std::filesystem::path& path, bool deinterlace = false);
    ~MpegSource();
    MpegSource(const MpegSource&) = delete;
    MpegSource& operator=(const MpegSource&) = delete;

    MpegInfo info() const;
    // Per-stream timestamp order. Frame borrowed until next() or seek().
    std::optional<MpegFrame> next();
    // Includes GOP preroll. Discard/trim output before the requested time.
    void seek(std::int64_t time);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
