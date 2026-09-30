#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace transcript {

enum class Kind { Choice, Caption, Marker };

struct Entry {
    Kind kind = Kind::Caption;
    std::wstring text;
    std::wstring movie;
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
    std::uint32_t scale = 0;
};

class History {
public:
    static constexpr std::size_t capacity = 20000;

    const std::vector<Entry>& entries() const {
        return entries_;
    }

    std::size_t evicted() const {
        return evicted_;
    }

    void record_choice(std::wstring text) {
        append({Kind::Choice, std::move(text), {}, 0, 0, 0});
    }

    void record_marker(std::wstring text) {
        append({Kind::Marker, std::move(text), {}, 0, 0, 0});
    }

    void record_caption(std::wstring movie, std::uint64_t begin, std::uint64_t end,
                        std::uint32_t scale, std::wstring text) {
        if (!scale || end <= begin || movie.empty() || text.empty()) {
            return;
        }
        const auto segment = std::find_if(entries_.rbegin(), entries_.rend(),
                                          [](const Entry& e) { return e.kind != Kind::Caption; });
        const auto duplicate = std::any_of(entries_.rbegin(), segment, [&](const Entry& e) {
            return e.movie == movie && e.begin == begin && e.end == end && e.scale == scale &&
                   e.text == text;
        });
        if (!duplicate) {
            append({Kind::Caption, std::move(text), std::move(movie), begin, end, scale});
        }
    }

    void clear() {
        entries_.clear();
        evicted_ = 0;
    }

private:
    void append(Entry entry) {
        if (entry.text.empty()) {
            return;
        }
        if (entries_.size() == capacity) {
            entries_.erase(entries_.begin());
            ++evicted_;
        }
        entries_.push_back(std::move(entry));
    }

    std::vector<Entry> entries_;
    std::size_t evicted_ = 0;
};

}
