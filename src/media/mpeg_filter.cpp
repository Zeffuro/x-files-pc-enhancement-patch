#include "mpeg_filter.h"

#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/frame.h>
}

namespace media::mpeg {
namespace {

void check(int result, const char* operation) {
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, message, sizeof(message));
        throw std::runtime_error(std::string(operation) + ": " + message);
    }
}

}

BwdifFilter::BwdifFilter(AVRational frame_rate) : frame_rate_(frame_rate) {
    output_ = av_frame_alloc();
    if (!output_) {
        throw std::bad_alloc();
    }
}

BwdifFilter::~BwdifFilter() {
    reset();
    av_frame_free(&output_);
}

void BwdifFilter::open(const AVFrame& frame) {
    graph_ = avfilter_graph_alloc();
    if (!graph_) {
        throw std::bad_alloc();
    }
    graph_->nb_threads = 1;
    graph_->thread_type = 0;
    const auto aspect = frame.sample_aspect_ratio.num > 0 && frame.sample_aspect_ratio.den > 0
                            ? frame.sample_aspect_ratio
                            : AVRational{1, 1};
    char arguments[256]{};
    const int size = std::snprintf(arguments, sizeof(arguments),
                                   "video_size=%dx%d:pix_fmt=%d:time_base=1/90000:"
                                   "pixel_aspect=%d/%d:frame_rate=%d/%d",
                                   frame.width, frame.height, frame.format, aspect.num, aspect.den,
                                   frame_rate_.num, frame_rate_.den);
    if (size < 0 || size >= static_cast<int>(sizeof(arguments))) {
        throw std::runtime_error("Invalid BWDIF source parameters");
    }
    const auto* buffer = avfilter_get_by_name("buffer");
    const auto* bwdif = avfilter_get_by_name("bwdif");
    const auto* buffersink = avfilter_get_by_name("buffersink");
    if (!buffer || !bwdif || !buffersink) {
        throw std::runtime_error("BWDIF filters are unavailable");
    }
    AVFilterContext* filter = nullptr;
    check(avfilter_graph_create_filter(&source_, buffer, "dvd_in", arguments, nullptr, graph_),
          "Create BWDIF source");
    auto* parameters = av_buffersrc_parameters_alloc();
    if (!parameters) {
        throw std::bad_alloc();
    }
    parameters->color_space = frame.colorspace;
    parameters->color_range = frame.color_range;
    parameters->alpha_mode = frame.alpha_mode;
    const int parameters_result = av_buffersrc_parameters_set(source_, parameters);
    av_free(parameters);
    check(parameters_result, "Set BWDIF source color");
    check(avfilter_graph_create_filter(&filter, bwdif, "dvd_bwdif",
                                       "mode=send_frame:parity=auto:deint=interlaced", nullptr,
                                       graph_),
          "Create BWDIF filter");
    check(avfilter_graph_create_filter(&sink_, buffersink, "dvd_out", nullptr, nullptr, graph_),
          "Create BWDIF sink");
    check(avfilter_link(source_, 0, filter, 0), "Link BWDIF source");
    check(avfilter_link(filter, 0, sink_, 0), "Link BWDIF sink");
    check(avfilter_graph_config(graph_, nullptr), "Configure BWDIF filter");
}

void BwdifFilter::push(const MpegFrame& input) {
    if (input.stream != MpegStream::video || !input.frame || finished_) {
        throw std::runtime_error("Invalid BWDIF input");
    }
    if (pending_.size() >= 8) {
        throw std::runtime_error("BWDIF lookahead exceeded the frame limit");
    }
    if (!graph_) {
        open(*input.frame);
    }
    AVFrame* copy = av_frame_clone(input.frame);
    if (!copy) {
        throw std::bad_alloc();
    }
    copy->pts = input.time;
    copy->duration = input.duration;
    const int result = av_buffersrc_add_frame_flags(source_, copy, 0);
    av_frame_free(&copy);
    check(result, "Send BWDIF frame");
    pending_.push_back({input.time, input.duration, input.estimated_time});
}

std::optional<MpegFrame> BwdifFilter::pull() {
    av_frame_unref(output_);
    if (!graph_ || drained_) {
        return std::nullopt;
    }
    const int result = av_buffersink_get_frame(sink_, output_);
    if (result == AVERROR(EAGAIN)) {
        if (finished_) {
            throw std::runtime_error("BWDIF drain did not finish");
        }
        return std::nullopt;
    }
    if (result == AVERROR_EOF) {
        drained_ = true;
        if (!pending_.empty()) {
            throw std::runtime_error("BWDIF dropped DVD frames");
        }
        return std::nullopt;
    }
    check(result, "Receive BWDIF frame");
    if (pending_.empty()) {
        throw std::runtime_error("BWDIF produced an extra DVD frame");
    }
    const auto metadata = pending_.front();
    pending_.pop_front();
    return MpegFrame{MpegStream::video, output_, metadata.time, metadata.duration,
                     metadata.estimated_time};
}

void BwdifFilter::finish() {
    if (finished_) {
        return;
    }
    finished_ = true;
    if (graph_) {
        check(av_buffersrc_add_frame_flags(source_, nullptr, 0), "Drain BWDIF filter");
    }
}

void BwdifFilter::reset() {
    avfilter_graph_free(&graph_);
    source_ = nullptr;
    sink_ = nullptr;
    pending_.clear();
    if (output_) {
        av_frame_unref(output_);
    }
    finished_ = false;
    drained_ = false;
}

}
