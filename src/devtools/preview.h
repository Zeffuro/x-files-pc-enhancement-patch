#pragma once
#include "media/video.h"
#include "media/frame_reference.h"
#include "media/subtitles.h"
#include "playback/audio.h"
#include <chrono>
#include <memory>

namespace devtools {
class Preview {
public:
    Preview(const std::filesystem::path& root, const std::filesystem::path& relative);
    void play();
    void pause();
    void seek(std::uint64_t milliseconds);
    void update();
    std::uint64_t time() const;
    std::uint64_t duration() const;

    bool frame_navigation() const {
        return frame_navigation_;
    }

    bool direct_frame() const {
        return direct_frame_;
    }

    std::size_t frame_count() const {
        return video_track_ ? video_track_->samples.size() : 0;
    }

    void select_frame(std::size_t sample);
    std::vector<std::size_t> video_tracks() const;
    std::optional<std::size_t> video_track() const;
    void select_video_track(std::size_t index);
    std::pair<unsigned, unsigned> frame_dimensions(std::size_t sample) const;

    bool playing() const {
        return playing_;
    }

    bool has_video() const {
        return video_track_ != nullptr;
    }

    bool has_audio() const {
        return audio_track_ != nullptr;
    }

    const media::Frame& frame() const {
        return frame_;
    }

    std::optional<media::FrameReference> image() const {
        return video_track_ && sample_ && !frame_.pixels.empty()
                   ? media::frame_reference(*video_track_, *sample_)
                   : std::nullopt;
    }

    std::wstring caption() const;

    void captions(std::vector<media::subtitles::Cue> cues) {
        cues_ = std::move(cues);
    }

    const std::vector<media::subtitles::Cue>& cues() const {
        return cues_;
    }

    const media::Movie& movie() const {
        return movie_;
    }

private:
    media::Movie movie_;
    const media::Track* video_track_ = nullptr;
    const media::Track* audio_track_ = nullptr;
    media::Video decoder_;
    media::Frame frame_;
    std::optional<std::size_t> sample_;
    std::unique_ptr<playback::Audio> audio_;
    std::vector<media::subtitles::Cue> cues_;
    std::chrono::steady_clock::time_point started_;
    std::uint64_t position_ = 0;
    bool playing_ = false;
    bool frame_navigation_ = false;
    bool direct_frame_ = false;
};
}
