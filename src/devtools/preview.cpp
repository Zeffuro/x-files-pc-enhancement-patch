#include "preview.h"
#include <algorithm>
#include <stdexcept>

namespace devtools {
namespace {
media::Movie open(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 512ull * 1024 * 1024) {
        throw std::runtime_error("Movie is too large to preview.");
    }
    return media::Movie::open(path);
}
}

Preview::Preview(const std::filesystem::path& root, const std::filesystem::path& relative)
    : movie_(open(root / relative)) {
    if (duration() > UINT32_MAX) {
        throw std::runtime_error("Movie duration is too long to preview.");
    }
    for (const auto& track : movie_.tracks) {
        if (track.handler == "vide" &&
            (!video_track_ || (!(video_track_->flags & 1) && (track.flags & 1)))) {
            video_track_ = &track;
        }
        if (track.handler == "soun" &&
            (!audio_track_ || (!(audio_track_->flags & 1) && (track.flags & 1)))) {
            audio_track_ = &track;
        }
    }
    if (auto override = media::subtitles::load_override(root, relative, root / relative)) {
        cues_ = std::move(*override);
    } else {
        cues_ = media::subtitles::native_cues(movie_);
        for (auto& cue : cues_) {
            cue.begin = cue.begin * 1000 / movie_.timescale;
            cue.end = (cue.end * 1000 + movie_.timescale - 1) / movie_.timescale;
        }
    }
    update();
}

std::uint64_t Preview::duration() const {
    return (movie_.duration * 1000 + movie_.timescale - 1) / movie_.timescale;
}

std::uint64_t Preview::time() const {
    const auto elapsed = playing_ ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - started_)
                                        .count()
                                  : 0;
    return std::min(duration(), position_ + static_cast<std::uint64_t>(elapsed));
}

void Preview::play() {
    if (playing_) {
        return;
    }
    if (position_ >= duration()) {
        position_ = 0;
    }
    if (audio_track_) {
        if (!audio_) {
            audio_ = std::make_unique<playback::Audio>(movie_, *audio_track_);
        }
        audio_->play(static_cast<std::uint32_t>(position_), 1000, 256);
    }
    started_ = std::chrono::steady_clock::now();
    playing_ = true;
}

void Preview::pause() {
    position_ = time();
    playing_ = false;
    if (audio_) {
        audio_->stop();
    }
}

void Preview::seek(std::uint64_t milliseconds) {
    const bool resume = playing_;
    pause();
    position_ = std::min(duration(), milliseconds);
    update();
    if (resume && position_ < duration()) {
        play();
    }
}

void Preview::update() {
    const auto position = time();
    if (position == duration()) {
        pause();
    }
    if (playing_ && audio_) {
        audio_->refresh(static_cast<std::uint32_t>(position), 1000);
    }
    if (video_track_) {
        const auto movie_time =
            std::min(movie_.duration ? movie_.duration - 1 : 0, position * movie_.timescale / 1000);
        const auto sample = video_track_->sample_at(movie_time, movie_.timescale);
        if (sample != sample_) {
            frame_ = sample ? decoder_.decode(movie_, *video_track_, *sample) : media::Frame{};
            sample_ = sample;
        }
    }
}

std::wstring Preview::caption() const {
    return media::subtitles::caption_at(cues_, time(), 1000);
}
}
