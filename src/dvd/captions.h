#pragma once

#include "media/subtitles.h"

namespace dvd {

class Captions {
public:
    Captions(std::vector<media::subtitles::Cue> cues, std::uint32_t scale,
             std::int64_t offset = -3609);
    static bool supported(const std::filesystem::path& vob);
    static Captions load(const std::filesystem::path& vob);
    std::wstring at(std::int64_t time) const;

    const std::vector<media::subtitles::Cue>& cues() const {
        return cues_;
    }

    const std::vector<media::subtitles::Cue>& source_cues() const {
        return source_cues_;
    }

    std::uint32_t source_scale() const {
        return source_scale_;
    }

private:
    Captions() = default;
    std::vector<media::subtitles::Cue> cues_;
    std::vector<media::subtitles::Cue> source_cues_;
    std::uint32_t source_scale_ = 0;
};

}
