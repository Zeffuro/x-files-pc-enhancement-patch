#include "tempo.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
}

namespace media {
namespace {

void check(int result, const char* operation) {
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, message, sizeof(message));
        throw std::runtime_error(std::string(operation) + ": " + message);
    }
}

}

struct Tempo::State {
    AVFilterGraph* graph = nullptr;
    AVFilterContext* source = nullptr;
    AVFilterContext* sink = nullptr;
    AVFrame* frame = nullptr;
    unsigned rate;
    unsigned channels;
    unsigned speed;
    std::size_t window = 1;
    std::vector<std::int16_t> input;
    std::vector<std::int16_t> pending;
    std::vector<std::int16_t> tail;
    std::uint64_t frames_in = 0;
    std::uint64_t frames_out = 0;
    std::int64_t filter_frames = 0;
    bool finished = false;

    State(unsigned sample_rate, unsigned channel_count, unsigned multiplier)
        : rate(sample_rate), channels(channel_count), speed(multiplier) {
        if (rate < 8000 || rate > 192000 || (channels != 1 && channels != 2) || speed < 2 ||
            speed > 4) {
            throw std::runtime_error("Invalid tempo audio format or speed");
        }
        while (window < rate / 24) {
            window *= 2;
        }
        input.reserve(window * channels);
        tail.reserve(window * channels);
    }

    ~State() {
        av_frame_free(&frame);
        avfilter_graph_free(&graph);
    }

    void open() {
        const auto* buffer = avfilter_get_by_name("abuffer");
        const auto* tempo = avfilter_get_by_name("atempo");
        const auto* output = avfilter_get_by_name("abuffersink");
        if (!buffer || !tempo || !output) {
            throw std::runtime_error("Tempo filters are unavailable");
        }
        graph = avfilter_graph_alloc();
        frame = av_frame_alloc();
        if (!graph || !frame) {
            throw std::bad_alloc();
        }
        graph->nb_threads = 1;
        graph->thread_type = 0;
        const auto arguments =
            "time_base=1/" + std::to_string(rate) + ":sample_rate=" + std::to_string(rate) +
            ":sample_fmt=s16:channel_layout=" + (channels == 1 ? "mono" : "stereo");
        AVFilterContext* filter = nullptr;
        check(avfilter_graph_create_filter(&source, buffer, "tempo_in", arguments.c_str(), nullptr,
                                           graph),
              "Create tempo source");
        check(avfilter_graph_create_filter(&filter, tempo, "tempo_2x", "tempo=2", nullptr, graph),
              "Create tempo filter");
        check(avfilter_link(source, 0, filter, 0), "Link tempo source");
        if (speed > 2) {
            AVFilterContext* second = nullptr;
            // Factors above 2 skip samples instead of blending them.
            check(avfilter_graph_create_filter(&second, tempo, "tempo_final",
                                               speed == 3 ? "tempo=1.5" : "tempo=2", nullptr,
                                               graph),
                  "Create final tempo filter");
            check(avfilter_link(filter, 0, second, 0), "Link tempo stages");
            filter = second;
        }
        check(avfilter_graph_create_filter(&sink, output, "tempo_out", nullptr, nullptr, graph),
              "Create tempo sink");
        check(avfilter_link(filter, 0, sink, 0), "Link tempo sink");
        check(avfilter_graph_config(graph, nullptr), "Configure tempo filter");
    }

    void drain() {
        for (;;) {
            av_frame_unref(frame);
            const int result = av_buffersink_get_frame(sink, frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            check(result, "Receive tempo samples");
            if (frame->format != AV_SAMPLE_FMT_S16 ||
                frame->ch_layout.nb_channels != static_cast<int>(channels) ||
                frame->sample_rate != static_cast<int>(rate) || frame->nb_samples < 0) {
                throw std::runtime_error("Unexpected tempo output format");
            }
            const auto* samples = reinterpret_cast<const std::int16_t*>(frame->data[0]);
            auto count = static_cast<std::size_t>(frame->nb_samples) * channels;
            if (finished) {
                const auto remaining = (frames_in / speed - frames_out) * channels;
                count = static_cast<std::size_t>(std::min<std::uint64_t>(
                    count, remaining > pending.size() ? remaining - pending.size() : 0));
            }
            if (pending.size() + count > window * channels * 8) {
                throw std::runtime_error("Tempo lookahead exceeded the sample limit");
            }
            pending.insert(pending.end(), samples, samples + count);
        }
    }

    void send(std::span<const std::int16_t> samples) {
        av_frame_unref(frame);
        frame->format = AV_SAMPLE_FMT_S16;
        frame->sample_rate = static_cast<int>(rate);
        frame->nb_samples = static_cast<int>(samples.size() / channels);
        frame->pts = filter_frames;
        av_channel_layout_default(&frame->ch_layout, static_cast<int>(channels));
        check(av_frame_get_buffer(frame, 0), "Allocate tempo samples");
        std::memcpy(frame->data[0], samples.data(), samples.size_bytes());
        filter_frames += frame->nb_samples;
        check(av_buffersrc_add_frame_flags(source, frame, 0), "Send tempo samples");
        drain();
    }

    void release(std::vector<std::int16_t>& result) {
        const auto held = window * channels;
        const auto available = pending.size() > held ? pending.size() - held : 0;
        const auto budget = (frames_in / speed - frames_out) * channels;
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(available, budget));
        result.insert(result.end(), pending.begin(), pending.begin() + count);
        pending.erase(pending.begin(), pending.begin() + count);
        frames_out += count / channels;
    }

    void remember(std::span<const std::int16_t> samples) {
        const auto limit = window * channels;
        if (samples.size() >= limit) {
            tail.assign(samples.end() - limit, samples.end());
            return;
        }
        if (tail.size() + samples.size() > limit) {
            tail.erase(tail.begin(), tail.begin() + tail.size() + samples.size() - limit);
        }
        tail.insert(tail.end(), samples.begin(), samples.end());
    }
};

Tempo::Tempo(unsigned sample_rate, unsigned channels, unsigned speed)
    : state_(std::make_unique<State>(sample_rate, channels, speed)) {
    state_->open();
}

Tempo::~Tempo() = default;

std::vector<std::int16_t> Tempo::push(std::span<const std::int16_t> samples) {
    auto& state = *state_;
    if (state.finished || samples.size() % state.channels != 0 ||
        samples.size() / state.channels >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - state.frames_in -
                state.window * 8) {
        throw std::runtime_error("Invalid tempo input");
    }
    std::vector<std::int16_t> output;
    const auto block = state.window / 2 * state.channels;
    while (!samples.empty()) {
        const auto count = std::min(samples.size(), block - state.input.size());
        const auto next = samples.first(count);
        state.remember(next);
        state.input.insert(state.input.end(), next.begin(), next.end());
        state.frames_in += count / state.channels;
        samples = samples.subspan(count);
        if (state.input.size() == block) {
            state.send(state.input);
            state.input.clear();
            state.release(output);
        }
    }
    return output;
}

std::vector<std::int16_t> Tempo::finish() {
    auto& state = *state_;
    if (state.finished) {
        return {};
    }
    state.finished = true;
    if (!state.input.empty()) {
        state.send(state.input);
        state.input.clear();
    }
    // Complete WSOLA lookahead without changing the requested duration.
    const std::vector<std::int16_t> padding(state.window / 2 * state.channels);
    for (unsigned i = 0; i < 16; ++i) {
        state.send(padding);
    }
    check(av_buffersrc_add_frame_flags(state.source, nullptr, 0), "Finish tempo input");
    state.drain();
    const auto needed = (state.frames_in / state.speed - state.frames_out) * state.channels;
    if (needed > state.window * state.channels * 8) {
        throw std::runtime_error("Tempo final tail exceeded the sample limit");
    }
    state.pending.resize(static_cast<std::size_t>(needed));

    // Align the retained final grain with the real endpoint, including tiny clips.
    const auto grain = std::min(state.pending.size(), state.tail.size()) / state.channels;
    for (std::size_t i = 0; i < grain; ++i) {
        const auto weight = static_cast<std::int64_t>(i + 1);
        for (unsigned channel = 0; channel < state.channels; ++channel) {
            auto& sample = state.pending[state.pending.size() - grain * state.channels +
                                         i * state.channels + channel];
            const auto real = state.tail[state.tail.size() - grain * state.channels +
                                         i * state.channels + channel];
            sample = static_cast<std::int16_t>(
                (sample * (static_cast<std::int64_t>(grain) - weight) + real * weight) /
                static_cast<std::int64_t>(grain));
        }
    }
    state.frames_out = state.frames_in / state.speed;
    return std::move(state.pending);
}

}
