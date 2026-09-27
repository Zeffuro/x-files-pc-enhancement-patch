#include "artwork.h"
#include "preview.h"
#include <algorithm>

namespace devtools {
ArtworkCache::ArtworkCache(std::filesystem::path root)
    : root_(std::move(root)), worker_([this] { run(); }) {}

ArtworkCache::~ArtworkCache() {
    {
        std::lock_guard lock(mutex_);
        stopped_ = true;
    }
    ready_.notify_one();
    worker_.join();
}

void ArtworkCache::request(const std::vector<std::filesystem::path>& visible) {
    std::lock_guard lock(mutex_);
    pending_.clear();
    for (const auto& path : visible) {
        if (const auto found = entries_.find(path); found != entries_.end()) {
            found->second.used = ++sequence_;
        } else if (path != active_ && pending_.size() < 64) {
            pending_.push_back(path);
        }
    }
    ready_.notify_one();
}

std::shared_ptr<const Artwork> ArtworkCache::get(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(path);
    return found == entries_.end() ? nullptr : found->second.artwork;
}

void ArtworkCache::run() {
    for (;;) {
        std::filesystem::path path;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stopped_ || !pending_.empty(); });
            if (stopped_) {
                return;
            }
            path = pending_.front();
            pending_.pop_front();
            active_ = path;
        }
        auto result = std::make_shared<Artwork>();
        try {
            Preview preview(root_, path);
            result->audio = preview.has_audio();
            const auto& source = preview.frame();
            if (!source.pixels.empty()) {
                auto& frame = result->frame;
                const auto scale = std::min(160.0 / source.width, 90.0 / source.height);
                frame.width = std::max(1u, static_cast<unsigned>(source.width * scale));
                frame.height = std::max(1u, static_cast<unsigned>(source.height * scale));
                frame.pixels.resize(frame.width * frame.height * 4);
                for (unsigned y = 0; y < frame.height; ++y) {
                    for (unsigned x = 0; x < frame.width; ++x) {
                        const auto from = (y * source.height / frame.height * source.width +
                                           x * source.width / frame.width) *
                                          4;
                        std::copy_n(source.pixels.data() + from, 4,
                                    frame.pixels.data() + (y * frame.width + x) * 4);
                    }
                }
            }
        } catch (...) {
            result->failed = true;
        }
        {
            std::lock_guard lock(mutex_);
            active_.clear();
            entries_[path] = {std::move(result), ++sequence_};
            if (entries_.size() > 96) {
                const auto oldest = std::min_element(
                    entries_.begin(), entries_.end(),
                    [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
                entries_.erase(oldest);
            }
        }
    }
}
}
