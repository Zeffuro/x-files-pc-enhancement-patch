#pragma once
#include "media/video.h"
#include <filesystem>
#include <memory>

namespace saves {
struct SceneReference {
    std::filesystem::path movie;
    std::uint32_t track = 0;
    std::uint64_t sample = 0;
    bool motion = false;
    std::uint64_t time = 0;
};

bool valid_scene_reference(const SceneReference& reference);
void write_scene_reference(const std::filesystem::path& save, const SceneReference& reference);
SceneReference read_scene_reference(const std::filesystem::path& save);

class ScenePreview {
public:
    ScenePreview(const std::filesystem::path& root, const SceneReference& reference);
    const media::Frame& update(std::uint64_t milliseconds);

    const media::Frame& frame() const {
        return decoder_.last_frame();
    }

private:
    media::Movie movie_;
    media::Video decoder_;
    const media::Track* track_ = nullptr;
    SceneReference reference_;
    std::optional<std::size_t> sample_;
};
}
