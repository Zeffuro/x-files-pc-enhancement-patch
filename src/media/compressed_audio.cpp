#include "compressed_audio.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}

#include <cstring>
#include <stdexcept>

namespace media {
namespace {

constexpr std::size_t sound_description_v1_size = 44;

void check(int result) {
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, message, sizeof(message));
        throw std::runtime_error(std::string("Audio decoder: ") + message);
    }
}

}

struct CompressedAudio::State {
    AVCodecContext* codec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwrContext* converter = nullptr;
    unsigned channels = 0;
    unsigned sample_rate = 0;

    ~State() {
        swr_free(&converter);
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec);
    }
};

CompressedAudio::CompressedAudio(const Description& description)
    : state_(std::make_unique<State>()) {
    auto& state = *state_;
    auto id = AV_CODEC_ID_NONE;
    if (description.codec == "QDMC") {
        id = AV_CODEC_ID_QDMC;
    } else if (description.codec == "QDM2") {
        id = AV_CODEC_ID_QDM2;
    }
    const auto decoder = avcodec_find_decoder(id);
    if (!decoder || description.bytes.size() <= sound_description_v1_size ||
        description.bytes.size() > INT_MAX) {
        throw std::runtime_error("Unsupported compressed audio description");
    }
    state.codec = avcodec_alloc_context3(decoder);
    state.frame = av_frame_alloc();
    state.packet = av_packet_alloc();
    if (!state.codec || !state.frame || !state.packet) {
        throw std::bad_alloc();
    }
    state.channels = description.channels;
    state.sample_rate = description.sample_rate;
    state.codec->sample_rate = description.sample_rate;
    av_channel_layout_default(&state.codec->ch_layout, description.channels);
    const auto extra = std::span(description.bytes).subspan(sound_description_v1_size);
    state.codec->extradata =
        static_cast<std::uint8_t*>(av_mallocz(extra.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!state.codec->extradata) {
        throw std::bad_alloc();
    }
    std::memcpy(state.codec->extradata, extra.data(), extra.size());
    state.codec->extradata_size = static_cast<int>(extra.size());
    check(avcodec_open2(state.codec, decoder, nullptr));
}

CompressedAudio::~CompressedAudio() = default;

std::vector<std::int16_t> CompressedAudio::decode(std::span<const std::uint8_t> bytes) {
    auto& state = *state_;
    if (bytes.size() > INT_MAX) {
        throw std::runtime_error("Compressed audio packet is too large");
    }
    av_packet_unref(state.packet);
    check(av_new_packet(state.packet, static_cast<int>(bytes.size())));
    std::memcpy(state.packet->data, bytes.data(), bytes.size());
    check(avcodec_send_packet(state.codec, state.packet));
    std::vector<std::int16_t> result;
    for (;;) {
        const auto status = avcodec_receive_frame(state.codec, state.frame);
        if (status == AVERROR(EAGAIN)) {
            break;
        }
        check(status);
        const auto& frame = *state.frame;
        if (frame.ch_layout.nb_channels != static_cast<int>(state.channels) ||
            frame.sample_rate != static_cast<int>(state.sample_rate) || frame.nb_samples < 0) {
            throw std::runtime_error("Compressed audio changed its output format");
        }
        if (!state.converter) {
            check(swr_alloc_set_opts2(&state.converter, &frame.ch_layout, AV_SAMPLE_FMT_S16,
                                      frame.sample_rate, &frame.ch_layout,
                                      static_cast<AVSampleFormat>(frame.format), frame.sample_rate,
                                      0, nullptr));
            check(swr_init(state.converter));
        }
        const auto offset = result.size();
        result.resize(offset + static_cast<std::size_t>(frame.nb_samples) * state.channels);
        auto output = reinterpret_cast<std::uint8_t*>(result.data() + offset);
        const auto converted =
            swr_convert(state.converter, &output, frame.nb_samples,
                        const_cast<const std::uint8_t**>(frame.extended_data), frame.nb_samples);
        check(converted);
        if (converted != frame.nb_samples) {
            throw std::runtime_error("Audio converter delayed samples");
        }
        av_frame_unref(state.frame);
    }
    return result;
}

}
