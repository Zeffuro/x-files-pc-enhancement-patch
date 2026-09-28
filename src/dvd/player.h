#pragma once

#include "media/mpeg.h"
#include <deque>
#include <limits>

namespace dvd {

class Sink {
public:
    virtual ~Sink() = default;
    virtual void video(const AVFrame& frame) = 0;
    virtual void audio(const AVFrame& frame, int first, int count) = 0;
    virtual bool drained() = 0;
    virtual void pause(bool value) = 0;
    virtual void clear() = 0;
};

enum class Status { ready, playing, paused, completed, stopped, failed };

class Player {
public:
    Player(const std::filesystem::path& path, Sink& sink, bool deinterlace = false);
    ~Player();
    media::MpegInfo info() const;
    void start(std::int64_t from = 0, std::int64_t to = std::numeric_limits<std::int64_t>::max(),
               bool paused = false);
    std::int64_t seek_end();
    void pump(std::int64_t time);
    void pause();
    void resume();
    void stop();
    Status status() const;
    std::int64_t position() const;
    bool audio_finished() const;

private:
    struct FrameDeleter {
        void operator()(AVFrame* frame) const;
    };

    struct Frame {
        std::unique_ptr<AVFrame, FrameDeleter> data;
        std::int64_t time;
        std::int64_t end;
        std::size_t bytes;
    };

    void fill(std::int64_t horizon);
    void submit(Frame& frame);
    media::MpegSource source_;
    Sink& sink_;
    std::deque<Frame> video_;
    std::deque<Frame> audio_;
    std::optional<media::MpegFrame> pending_;
    Status status_ = Status::ready;
    std::int64_t from_ = 0;
    std::int64_t to_ = 0;
    std::int64_t position_ = 0;
    std::int64_t end_ = 0;
    std::int64_t samples_ = 0;
    std::int64_t video_read_ = 0;
    std::int64_t audio_read_ = 0;
    bool eof_ = false;
    bool audio_anchored_ = false;
    std::size_t queue_bytes_ = 0;
};

}
