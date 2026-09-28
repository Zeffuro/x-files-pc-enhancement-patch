#include "mpeg.h"
#include "mpeg_filter.h"
#include "mpeg_timing.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace media {
namespace {

void check(int result, const char* operation) {
    if (result < 0) {
        char text[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, text, sizeof(text));
        throw std::runtime_error(std::string(operation) + ": " + text);
    }
}

bool valid(AVRational value) {
    return value.num > 0 && value.den > 0;
}

struct Decoder {
    AVCodecContext* codec = nullptr;
    AVStream* stream = nullptr;
    mpeg::Timeline timeline;
    bool draining = false;
    bool ended = false;

    ~Decoder() {
        avcodec_free_context(&codec);
    }

    void open(AVStream* input) {
        stream = input;
        codec = avcodec_alloc_context3(avcodec_find_decoder(input->codecpar->codec_id));
        if (!codec) {
            throw std::bad_alloc();
        }
        check(avcodec_parameters_to_context(codec, input->codecpar), "MPEG parameters");
        codec->pkt_timebase = input->time_base;
        codec->thread_count = 1;
        codec->max_pixels = 720 * 576;
        codec->err_recognition = AV_EF_CRCCHECK | AV_EF_EXPLODE;
        check(avcodec_open2(codec, avcodec_find_decoder(input->codecpar->codec_id), nullptr),
              "MPEG decoder");
    }
};

}

struct MpegSource::State {
    std::filesystem::path path;
    bool deinterlace = false;
    AVFormatContext* format = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    std::array<Decoder, 2> decoders;
    MpegInfo info;
    std::unique_ptr<mpeg::BwdifFilter> filter;
    int active = -1;
    bool eof = false;
    bool failed = false;

    ~State() {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avformat_close_input(&format);
    }

    void open(const std::filesystem::path& input, bool use_deinterlace) {
        path = std::filesystem::absolute(input);
        deinterlace = use_deinterlace;
        if (!std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("MPEG source must be an ordinary file");
        }
        format = avformat_alloc_context();
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!format || !packet || !frame) {
            throw std::bad_alloc();
        }
        format->probesize = 2 * 1024 * 1024;
        format->max_analyze_duration = 2 * AV_TIME_BASE;
        format->max_probe_packets = 256;
        format->max_index_size = 256 * 1024;
        format->max_streams = 8;
        format->error_recognition = AV_EF_EXPLODE;
        const auto name = std::filesystem::absolute(path).u8string();
        const auto* demuxer = av_find_input_format("mpeg");
        if (!demuxer) {
            throw std::runtime_error("MPEG program stream demuxer is unavailable");
        }
        check(avformat_open_input(&format, reinterpret_cast<const char*>(name.c_str()), demuxer,
                                  nullptr),
              "Open MPEG file");
        check(avformat_find_stream_info(format, nullptr), "Probe MPEG streams");
        for (unsigned i = 0; i < format->nb_streams; ++i) {
            auto* stream = format->streams[i];
            const auto* parameters = stream->codecpar;
            if (parameters->codec_type == AVMEDIA_TYPE_DATA) {
                continue;
            }
            const int slot = parameters->codec_id == AV_CODEC_ID_MPEG2VIDEO ? 0
                             : parameters->codec_id == AV_CODEC_ID_PCM_DVD  ? 1
                                                                            : -1;
            if (slot < 0 || decoders[slot].stream || !valid(stream->time_base) ||
                stream->start_time == AV_NOPTS_VALUE) {
                throw std::runtime_error("Unsupported or untimed MPEG stream");
            }
            if (slot == 0 && (parameters->width <= 0 || parameters->width > 720 ||
                              parameters->height <= 0 || parameters->height > 576)) {
                throw std::runtime_error("Unsupported DVD video dimensions");
            }
            if (slot == 1 &&
                (parameters->sample_rate != 48000 || parameters->ch_layout.nb_channels != 2)) {
                throw std::runtime_error("Unsupported DVD audio format");
            }
            decoders[slot].open(stream);
        }
        if (!decoders[0].stream || !decoders[1].stream) {
            throw std::runtime_error("DVD MPEG requires MPEG-2 video and DVD PCM audio");
        }
        const auto* video = decoders[0].stream;
        const auto* audio = decoders[1].stream;
        info.width = video->codecpar->width;
        info.height = video->codecpar->height;
        info.frame_rate = av_guess_frame_rate(format, decoders[0].stream, nullptr);
        info.sample_aspect_ratio =
            av_guess_sample_aspect_ratio(format, decoders[0].stream, nullptr);
        info.field_order = video->codecpar->field_order;
        info.sample_rate = audio->codecpar->sample_rate;
        info.channels = audio->codecpar->ch_layout.nb_channels;
        if (!valid(info.frame_rate) || !valid(info.sample_aspect_ratio)) {
            throw std::runtime_error("Missing DVD frame rate or aspect ratio");
        }
        info.video_start = av_rescale_q(video->start_time, video->time_base, mpeg::clock);
        info.audio_start = av_rescale_q(audio->start_time, audio->time_base, mpeg::clock);
        info.origin = std::min(info.video_start, info.audio_start);
        info.video_start -= info.origin;
        info.audio_start -= info.origin;
        decoders[0].timeline.reset(info.video_start);
        decoders[1].timeline.reset(info.audio_start);
        if (deinterlace) {
            filter = std::make_unique<mpeg::BwdifFilter>(info.frame_rate);
        }
    }

    MpegFrame output(Decoder& decoder) {
        if ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags) {
            throw std::runtime_error("Corrupt MPEG frame");
        }
        const bool video = active == 0;
        if (video && (frame->width != info.width || frame->height != info.height)) {
            throw std::runtime_error("DVD video dimensions changed");
        }
        if (!video && (frame->sample_rate != info.sample_rate ||
                       frame->ch_layout.nb_channels != info.channels || frame->nb_samples <= 0)) {
            throw std::runtime_error("DVD audio format changed");
        }
        auto duration = video ? av_rescale_q(1, av_inv_q(info.frame_rate), mpeg::clock)
                              : av_rescale_q(frame->nb_samples, {1, info.sample_rate}, mpeg::clock);
        if (video && frame->duration > 0) {
            duration = av_rescale_q(frame->duration, decoder.stream->time_base, mpeg::clock);
        } else if (video && frame->repeat_pict > 0) {
            duration = duration * (2 + frame->repeat_pict) / 2;
        }
        const auto pts = frame->best_effort_timestamp;
        const auto time =
            decoder.timeline.stamp(pts, decoder.stream->time_base, info.origin, duration);
        return {video ? MpegStream::video : MpegStream::audio, frame, time, duration,
                pts == AV_NOPTS_VALUE};
    }

    std::optional<MpegFrame> next() {
        av_frame_unref(frame);
        for (;;) {
            if (filter) {
                if (auto result = filter->pull()) {
                    return result;
                }
            }
            if (active >= 0) {
                auto& decoder = decoders[active];
                const auto result = avcodec_receive_frame(decoder.codec, frame);
                if (result >= 0) {
                    const auto decoded = output(decoder);
                    if (filter && decoded.stream == MpegStream::video) {
                        filter->push(decoded);
                        av_frame_unref(frame);
                        continue;
                    }
                    return decoded;
                }
                if (result == AVERROR_EOF) {
                    decoder.ended = true;
                } else if (result != AVERROR(EAGAIN) || decoder.draining) {
                    check(result, "Decode MPEG frame");
                }
                active = -1;
            }
            if (eof) {
                for (int i = 0; i < 2; ++i) {
                    auto& decoder = decoders[i];
                    if (!decoder.ended) {
                        check(avcodec_send_packet(decoder.codec, nullptr), "Drain MPEG decoder");
                        decoder.draining = true;
                        active = i;
                        break;
                    }
                }
                if (active < 0) {
                    if (filter) {
                        filter->finish();
                        if (auto result = filter->pull()) {
                            return result;
                        }
                    }
                    return std::nullopt;
                }
                continue;
            }
            const auto result = av_read_frame(format, packet);
            if (result == AVERROR_EOF) {
                if (format->pb && format->pb->error < 0) {
                    check(format->pb->error, "Read MPEG file");
                }
                eof = true;
                continue;
            }
            check(result, "Read MPEG packet");
            for (int i = 0; i < 2; ++i) {
                if (packet->stream_index == decoders[i].stream->index) {
                    active = i;
                    break;
                }
            }
            if (active >= 0) {
                if ((packet->flags & AV_PKT_FLAG_CORRUPT) || packet->size > 4 * 1024 * 1024) {
                    throw std::runtime_error("Corrupt or oversized MPEG packet");
                }
                check(avcodec_send_packet(decoders[active].codec, packet), "Send MPEG packet");
            }
            av_packet_unref(packet);
        }
    }

    void seek(std::int64_t time) {
        if (time < 0 || time > INT64_MAX - std::max<std::int64_t>(info.origin, 0)) {
            throw std::runtime_error("Invalid MPEG seek time");
        }
        auto* video = decoders[0].stream;
        const auto target = av_rescale_q(time - 90000 + info.origin, mpeg::clock, video->time_base);
        check(av_seek_frame(format, video->index, target, AVSEEK_FLAG_BACKWARD), "Seek MPEG file");
        av_packet_unref(packet);
        av_frame_unref(frame);
        if (filter) {
            filter->reset();
        }
        active = -1;
        eof = false;
        for (int i = 0; i < 2; ++i) {
            auto& decoder = decoders[i];
            avcodec_flush_buffers(decoder.codec);
            decoder.draining = decoder.ended = false;
            // A seek must acquire a new timestamp anchor, never inherit the old decode clock.
            decoder.timeline.reset(std::nullopt);
        }
    }
};

MpegSource::MpegSource(const std::filesystem::path& path, bool deinterlace)
    : state_(std::make_unique<State>()) {
    state_->open(path, deinterlace);
}

MpegSource::~MpegSource() = default;

MpegInfo MpegSource::info() const {
    return state_->info;
}

std::optional<MpegFrame> MpegSource::next() {
    if (state_->failed) {
        throw std::runtime_error("MPEG source failed, reopen before decoding");
    }
    try {
        return state_->next();
    } catch (...) {
        state_->failed = true;
        throw;
    }
}

void MpegSource::seek(std::int64_t time) {
    if (state_->failed) {
        throw std::runtime_error("MPEG source failed, reopen before seeking");
    }
    try {
        if (time >= 0 && time <= 90000) {
            // Reopening retains audio packets that precede the first video seek point.
            auto restart = std::make_unique<State>();
            restart->open(state_->path, state_->deinterlace);
            state_ = std::move(restart);
        } else {
            state_->seek(time);
        }
    } catch (...) {
        state_->failed = true;
        throw;
    }
}

}
