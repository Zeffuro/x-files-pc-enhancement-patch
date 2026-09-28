#pragma once

#include "mpeg.h"

#include <cstdint>
#include <deque>
#include <optional>

struct AVFilterContext;
struct AVFilterGraph;

namespace media::mpeg {

class BwdifFilter {
public:
    explicit BwdifFilter(AVRational frame_rate);
    ~BwdifFilter();
    BwdifFilter(const BwdifFilter&) = delete;
    BwdifFilter& operator=(const BwdifFilter&) = delete;

    void push(const MpegFrame& input);
    std::optional<MpegFrame> pull();
    void finish();
    void reset();

private:
    struct Metadata {
        std::int64_t time;
        std::int64_t duration;
        bool estimated_time;
    };

    void open(const AVFrame& frame);

    AVRational frame_rate_;
    AVFilterGraph* graph_ = nullptr;
    AVFilterContext* source_ = nullptr;
    AVFilterContext* sink_ = nullptr;
    AVFrame* output_ = nullptr;
    std::deque<Metadata> pending_;
    bool finished_ = false;
    bool drained_ = false;
};

}
