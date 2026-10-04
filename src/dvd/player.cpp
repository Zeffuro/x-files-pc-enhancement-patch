#include "player.h"
#include "media/mpeg_timing.h"

#include <algorithm>
#include <stdexcept>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

namespace dvd {
namespace {
constexpr AVRational samples{1, 48000};

std::int64_t sample_at(std::int64_t time) {
    return av_rescale_q(time, media::mpeg::clock, samples);
}
}

void Player::FrameDeleter::operator()(AVFrame* frame) const {
    av_frame_free(&frame);
}

Player::Player(const std::filesystem::path& path, Sink& sink, bool deinterlace)
    : source_(path, deinterlace), sink_(sink) {}

Player::~Player() = default;

media::MpegInfo Player::info() const {
    return source_.info();
}

void Player::start(std::int64_t from, std::int64_t to, bool paused) {
    try {
        if (from < 0 || to <= from) {
            throw std::runtime_error("Invalid DVD playback range");
        }
        sink_.clear();
        sink_.pause(true);
        video_.clear();
        audio_.clear();
        pending_.reset();
        queue_bytes_ = 0;
        source_.seek(from);
        from_ = position_ = end_ = from;
        to_ = to;
        samples_ = sample_at(from);
        video_read_ = audio_read_ = 0;
        eof_ = false;
        audio_anchored_ = false;
        audio_finished_ = false;
        status_ = Status::playing;
        pump(from);
        if (paused) {
            status_ = Status::paused;
        } else {
            sink_.pause(false);
        }
    } catch (...) {
        status_ = Status::failed;
        sink_.clear();
        throw;
    }
}

std::int64_t Player::seek_end() {
    stop();
    source_.seek(0);
    std::int64_t last = 0;
    while (const auto frame = source_.next()) {
        if (frame->stream == media::MpegStream::video) {
            last = frame->time;
        }
    }
    start(last, INT64_MAX, true);
    return last;
}

void Player::fill(std::int64_t horizon) {
    while (!eof_ && (pending_ || video_read_ < horizon || audio_read_ < horizon)) {
        const auto frame = pending_ ? pending_ : source_.next();
        if (!frame) {
            eof_ = true;
            break;
        }
        const auto end = frame->time + frame->duration;
        const bool video = frame->stream == media::MpegStream::video;
        if (end <= from_ || frame->time >= to_) {
            (video ? video_read_ : audio_read_) = end;
            pending_.reset();
            continue;
        }
        auto& queue = video ? video_ : audio_;
        std::size_t bytes = 0;
        for (const auto* buffer : frame->frame->buf) {
            if (buffer) {
                bytes += buffer->size;
            }
        }
        for (int i = 0; i < frame->frame->nb_extended_buf; ++i) {
            bytes += frame->frame->extended_buf[i]->size;
        }
        constexpr std::size_t max_bytes = 24 * 1024 * 1024;
        if (bytes > max_bytes) {
            throw std::runtime_error("DVD frame exceeds the playback buffer limit");
        }
        if (queue.size() >= (video ? 48u : 256u) || queue_bytes_ > max_bytes - bytes) {
            pending_ = frame;
            return;
        }
        std::unique_ptr<AVFrame, FrameDeleter> copy(av_frame_clone(frame->frame));
        if (!copy) {
            throw std::bad_alloc();
        }
        queue.push_back({std::move(copy), frame->time, std::min(end, to_), bytes});
        queue_bytes_ += bytes;
        (video ? video_read_ : audio_read_) = end;
        pending_.reset();
        end_ = std::max(end_, std::min(end, to_));
    }
    if (video_read_ >= to_ && audio_read_ >= to_) {
        eof_ = true;
    }
}

void Player::submit(Frame& item) {
    const auto& frame = *item.data;
    const auto begin = sample_at(item.time);
    const auto gap = begin - samples_;
    if (audio_anchored_ && std::abs(gap) > 2400) {
        throw std::runtime_error("Discontinuous DVD audio timestamps");
    }
    if (!audio_anchored_ && gap > 0) {
        if (gap > 48000) {
            throw std::runtime_error("DVD audio timestamp gap exceeds one second");
        }
        std::unique_ptr<AVFrame, FrameDeleter> silence(av_frame_alloc());
        if (!silence) {
            throw std::bad_alloc();
        }
        silence->format = frame.format;
        silence->sample_rate = 48000;
        silence->nb_samples = static_cast<int>(gap);
        if (av_channel_layout_copy(&silence->ch_layout, &frame.ch_layout) < 0 ||
            av_frame_get_buffer(silence.get(), 0) < 0 ||
            av_samples_set_silence(silence->extended_data, 0, silence->nb_samples, 2,
                                   static_cast<AVSampleFormat>(silence->format)) < 0) {
            throw std::runtime_error("Cannot create DVD audio silence");
        }
        sink_.audio(*silence, 0, silence->nb_samples);
        samples_ += gap;
    }
    // Packet timestamp jitter must not discard or duplicate continuous PCM.
    const auto first = audio_anchored_ ? 0
                                       : std::clamp(samples_ - begin, std::int64_t{0},
                                                    static_cast<std::int64_t>(frame.nb_samples));
    const auto count = std::min(frame.nb_samples - first, sample_at(to_) - samples_);
    if (count > 0) {
        sink_.audio(frame, static_cast<int>(first), static_cast<int>(count));
        samples_ += count;
    }
    audio_anchored_ = true;
}

void Player::pump(std::int64_t time) {
    if (status_ != Status::playing) {
        return;
    }
    try {
        const auto ahead = sink_.audio_horizon();
        if (ahead < 18000 || ahead > 90000 || time < position_ || time > INT64_MAX - ahead) {
            throw std::runtime_error("Invalid DVD presentation clock");
        }
        position_ = time;
        const auto horizon = std::min(time + ahead, to_);
        fill(horizon);
        while (!audio_.empty() && (!audio_anchored_ || audio_.front().time < horizon)) {
            submit(audio_.front());
            queue_bytes_ -= audio_.front().bytes;
            audio_.pop_front();
        }
        if (eof_ && audio_.empty() && !audio_finished_) {
            sink_.finish_audio();
            audio_finished_ = true;
        }
        while (!video_.empty() && video_.front().time <= time) {
            sink_.video(*video_.front().data);
            queue_bytes_ -= video_.front().bytes;
            video_.pop_front();
        }
        if (eof_ && video_.empty() && audio_.empty() && time >= end_ && sink_.drained()) {
            status_ = Status::completed;
        }
    } catch (...) {
        status_ = Status::failed;
        sink_.clear();
        throw;
    }
}

void Player::pause() {
    if (status_ == Status::playing) {
        sink_.pause(true);
        status_ = Status::paused;
    }
}

void Player::resume() {
    if (status_ == Status::paused) {
        sink_.pause(false);
        status_ = Status::playing;
    }
}

void Player::stop() {
    sink_.clear();
    video_.clear();
    audio_.clear();
    pending_.reset();
    queue_bytes_ = 0;
    status_ = Status::stopped;
}

Status Player::status() const {
    return status_;
}

std::int64_t Player::position() const {
    return position_;
}

bool Player::audio_finished() const {
    return audio_finished_;
}

}
