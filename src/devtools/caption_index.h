#pragma once
#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <set>
#include <thread>
#include <vector>

namespace devtools {
enum class CaptionCoverage { pending, none, present, unreadable };

class CaptionIndex {
public:
    ~CaptionIndex();
    void start(std::filesystem::path root, std::vector<std::filesystem::path> paths);
    bool contains(const std::filesystem::path& path, const std::wstring& query) const;

    CaptionCoverage coverage(const std::filesystem::path& path) const;

    unsigned completed() const {
        return completed_;
    }

    unsigned skipped() const {
        return skipped_;
    }

    unsigned total() const {
        return total_;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::wstring, std::wstring> text_;
    std::set<std::wstring> unreadable_;
    std::atomic<unsigned> completed_{0}, skipped_{0};
    unsigned total_ = 0;
    std::jthread worker_;
};
}
