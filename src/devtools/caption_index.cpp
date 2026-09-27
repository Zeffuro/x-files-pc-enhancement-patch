#include "caption_index.h"
#include "clip_catalog.h"
#include "media/subtitles.h"

namespace devtools {
CaptionIndex::~CaptionIndex() = default;

void CaptionIndex::start(std::filesystem::path root, std::vector<std::filesystem::path> paths) {
    if (worker_.joinable()) {
        return;
    }
    total_ = static_cast<unsigned>(paths.size());
    worker_ = std::jthread(
        [this, root = std::move(root), paths = std::move(paths)](std::stop_token stop) {
            std::size_t total_chars = 0;
            for (const auto& path : paths) {
                if (stop.stop_requested()) {
                    break;
                }
                try {
                    if (std::filesystem::file_size(root / path) > 512ull * 1024 * 1024) {
                        throw std::runtime_error("Movie too large");
                    }
                    auto cues = media::subtitles::load_override(root, path, root / path);
                    if (!cues) {
                        cues = media::subtitles::native_cues(media::Movie::open(root / path));
                    }
                    std::wstring text;
                    for (const auto& cue : *cues) {
                        if (text.size() + cue.text.size() > 1024 * 1024) {
                            throw std::runtime_error("Caption index entry too large");
                        }
                        text += cue.text + L"\n";
                    }
                    total_chars += text.size();
                    if (total_chars > 32 * 1024 * 1024) {
                        throw std::runtime_error("Caption index full");
                    }
                    std::lock_guard lock(mutex_);
                    text_.emplace(catalog_key(path), lower(std::move(text)));
                } catch (...) {
                    std::lock_guard lock(mutex_);
                    unreadable_.insert(catalog_key(path));
                    ++skipped_;
                }
                ++completed_;
            }
        });
}

CaptionCoverage CaptionIndex::coverage(const std::filesystem::path& path) const {
    std::lock_guard lock(mutex_);
    const auto key = catalog_key(path);
    if (unreadable_.contains(key)) {
        return CaptionCoverage::unreadable;
    }
    const auto found = text_.find(key);
    return found == text_.end()    ? CaptionCoverage::pending
           : found->second.empty() ? CaptionCoverage::none
                                   : CaptionCoverage::present;
}

bool CaptionIndex::contains(const std::filesystem::path& path, const std::wstring& query) const {
    std::lock_guard lock(mutex_);
    const auto it = text_.find(catalog_key(path));
    return it != text_.end() && it->second.find(query) != std::wstring::npos;
}
}
