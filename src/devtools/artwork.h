#pragma once
#include "media/video.h"
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace devtools {
struct Artwork {
    media::Frame frame;
    bool audio = false;
    bool failed = false;
};

class ArtworkCache {
public:
    explicit ArtworkCache(std::filesystem::path root);
    ~ArtworkCache();
    void request(const std::vector<std::filesystem::path>& visible);
    std::shared_ptr<const Artwork> get(const std::filesystem::path& path);

private:
    struct Entry {
        std::shared_ptr<const Artwork> artwork;
        std::uint64_t used;
    };

    void run();
    std::filesystem::path root_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::map<std::filesystem::path, Entry> entries_;
    std::deque<std::filesystem::path> pending_;
    std::filesystem::path active_;
    std::uint64_t sequence_ = 0;
    bool stopped_ = false;
    std::thread worker_;
};
}
