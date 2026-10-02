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
    auto extension = relative.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), std::towlower);
    frame_navigation_ = extension == L".nmv";
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
    if (frame_navigation_ && frame_count()) {
        select_frame(0);
    } else {
        update();
    }
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
    direct_frame_ = false;
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
    direct_frame_ = false;
    const bool resume = playing_;
    pause();
    position_ = std::min(duration(), milliseconds);
    update();
    if (resume && position_ < duration()) {
        play();
    }
}

void Preview::update() {
    if (direct_frame_) {
        return;
    }
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

void Preview::select_frame(std::size_t sample) {
    if (!frame_navigation_ || !video_track_ || sample >= frame_count()) {
        return;
    }
    pause();
    direct_frame_ = true;
    if (sample_ != sample) {
        auto frame = decoder_.decode(movie_, *video_track_, sample);
        frame_ = std::move(frame);
        sample_ = sample;
    }
}

std::vector<std::size_t> Preview::video_tracks() const {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < movie_.tracks.size(); ++index) {
        if (movie_.tracks[index].handler == "vide") {
            result.push_back(index);
        }
    }
    return result;
}

std::optional<std::size_t> Preview::video_track() const {
    return video_track_ ? std::optional<std::size_t>(video_track_ - movie_.tracks.data())
                        : std::nullopt;
}

void Preview::select_video_track(std::size_t index) {
    if (!frame_navigation_ || index >= movie_.tracks.size() ||
        movie_.tracks[index].handler != "vide") {
        return;
    }
    pause();
    if (video_track_ != &movie_.tracks[index]) {
        video_track_ = &movie_.tracks[index];
        sample_.reset();
        frame_ = {};
    }
    direct_frame_ = true;
    if (frame_count()) {
        select_frame(0);
    }
}

std::pair<unsigned, unsigned> Preview::frame_dimensions(std::size_t sample) const {
    if (!video_track_ || sample >= frame_count()) {
        return {};
    }
    const auto description = video_track_->samples[sample].description;
    if (description >= video_track_->descriptions.size()) {
        return {};
    }
    const auto& image = video_track_->descriptions[description];
    return {image.width, image.height};
}

std::wstring Preview::caption() const {
    return media::subtitles::caption_at(cues_, time(), 1000);
}
}
