#include "video.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <stdexcept>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace media {
namespace {

using Microsoft::WRL::ComPtr;

void check(HRESULT result) {
    if (FAILED(result)) {
        throw std::runtime_error("Windows image decoder failed (" + std::to_string(result) + ")");
    }
}

void dimensions(Frame& frame, unsigned width, unsigned height) {
    if (!width || !height || width > 4096 || height > 4096) {
        throw std::runtime_error("Invalid video frame dimensions");
    }
    frame.width = width;
    frame.height = height;
    frame.pixels.resize(width * height * 4);
}

}

struct Video::State {
    Frame frame;
    const Track* track = nullptr;
    std::optional<std::size_t> previous;
    std::uint32_t description = UINT32_MAX;
    AVCodecContext* codec = nullptr;
    AVFrame* decoded = nullptr;
    AVPacket* encoded = nullptr;
    SwsContext* colours = nullptr;
    bool com_initialized = false;
    ComPtr<IWICImagingFactory> factory;

    ~State() {
        reset_decoder();
        factory.Reset();
        if (com_initialized) {
            CoUninitialize();
        }
    }

    void reset_decoder() {
        sws_freeContext(colours);
        colours = nullptr;
        av_packet_free(&encoded);
        av_frame_free(&decoded);
        avcodec_free_context(&codec);
    }

    void jpeg(std::span<const std::uint8_t> packet) {
        if (!factory) {
            const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (result != RPC_E_CHANGED_MODE) {
                check(result);
            }
            com_initialized = SUCCEEDED(result);
            check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory)));
        }
        ComPtr<IWICStream> stream;
        check(factory->CreateStream(&stream));
        check(stream->InitializeFromMemory(const_cast<BYTE*>(packet.data()),
                                           static_cast<DWORD>(packet.size())));
        ComPtr<IWICBitmapDecoder> decoder;
        check(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                               &decoder));
        ComPtr<IWICBitmapFrameDecode> image;
        check(decoder->GetFrame(0, &image));
        UINT width = 0, height = 0;
        check(image->GetSize(&width, &height));
        dimensions(frame, width, height);
        ComPtr<IWICFormatConverter> converter;
        check(factory->CreateFormatConverter(&converter));
        check(converter->Initialize(image.Get(), GUID_WICPixelFormat32bppBGRA,
                                    WICBitmapDitherTypeNone, nullptr, 0,
                                    WICBitmapPaletteTypeCustom));
        check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(frame.pixels.size()),
                                    frame.pixels.data()));
    }

    void motion_jpeg(const Description& format, std::span<const std::uint8_t> packet) {
        auto number = [](std::span<const std::uint8_t> bytes, std::size_t offset, unsigned length) {
            if (offset > bytes.size() || length > bytes.size() - offset) {
                throw std::runtime_error("Truncated Motion JPEG header");
            }
            std::uint32_t value = 0;
            for (unsigned i = 0; i < length; ++i) {
                value = (value << 8) | bytes[offset + i];
            }
            return value;
        };
        auto next_field = [&](std::span<const std::uint8_t> bytes) -> std::size_t {
            if (number(bytes, 0, 2) != 0xffd8) {
                throw std::runtime_error("Missing JPEG start marker");
            }
            for (std::size_t offset = 2; offset < bytes.size();) {
                const auto marker = number(bytes, offset, 2);
                if (marker == 0xffda || marker == 0xffd9) {
                    break;
                }
                const auto length = number(bytes, offset + 2, 2);
                if (length < 2 || length > bytes.size() - offset - 2) {
                    throw std::runtime_error("Invalid JPEG marker length");
                }
                if (marker == 0xffe1 && length >= 22 &&
                    number(bytes, offset + 8, 4) == 0x6d6a7067) {
                    return number(bytes, offset + 20, 4);
                }
                offset += length + 2;
            }
            throw std::runtime_error("Missing Motion JPEG field header");
        };
        const auto split = next_field(packet);
        if (!split) {
            jpeg(packet);
            if (frame.width != format.width || frame.height != format.height) {
                throw std::runtime_error("Motion JPEG frame dimensions disagree");
            }
            return;
        }
        if (split >= packet.size() || next_field(packet.subspan(split)) != 0) {
            throw std::runtime_error("Invalid Motion JPEG field chain");
        }
        jpeg(packet.first(split));
        Frame first = std::move(frame);
        jpeg(packet.subspan(split));
        Frame second = std::move(frame);
        if (first.width != format.width || second.width != format.width ||
            first.height != second.height || first.height * 2 != format.height) {
            throw std::runtime_error("Motion JPEG field dimensions disagree");
        }
        unsigned parity = 0;
        for (std::size_t offset = 78; offset + 8 <= format.bytes.size();) {
            const auto length = number(format.bytes, offset, 4);
            if (length < 8 || length > format.bytes.size() - offset) {
                throw std::runtime_error("Invalid Motion JPEG description extension");
            }
            if (number(format.bytes, offset + 4, 4) == 0x6669656c) {
                if (length != 10 || format.bytes[offset + 8] != 2) {
                    throw std::runtime_error("Unsupported Motion JPEG field layout");
                }
                const auto detail = format.bytes[offset + 9];
                if (detail != 1 && detail != 6) {
                    throw std::runtime_error("Unsupported Motion JPEG field order");
                }
                parity = detail == 6 ? 1 : 0;
            }
            offset += length;
        }
        dimensions(frame, format.width, format.height);
        const auto stride = frame.width * 4;
        for (unsigned row = 0; row < first.height; ++row) {
            std::copy_n(first.pixels.data() + row * stride, stride,
                        frame.pixels.data() + (row * 2 + parity) * stride);
            std::copy_n(second.pixels.data() + row * stride, stride,
                        frame.pixels.data() + (row * 2 + 1 - parity) * stride);
        }
    }

    void compressed_frame(const Description& format, std::span<const std::uint8_t> bytes,
                          bool keyframe) {
        if (format.codec == "rpza") {
            if (bytes.size() < 4 || bytes[0] != 0xe1) {
                throw std::runtime_error("Invalid Apple Video frame header");
            }
            const std::size_t length = (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
            if (length < 4 || length > bytes.size()) {
                throw std::runtime_error("Truncated Apple Video frame");
            }
            // Some samples contain an extra frame after the declared RPZA chunk.
            bytes = bytes.first(length);
        }
        if (!codec) {
            dimensions(frame, format.width, format.height);
            const auto id = format.codec == "cvid" ? AV_CODEC_ID_CINEPAK : AV_CODEC_ID_RPZA;
            const auto decoder = avcodec_find_decoder(id);
            if (!decoder) {
                throw std::runtime_error("Video decoder is unavailable");
            }
            codec = avcodec_alloc_context3(decoder);
            decoded = av_frame_alloc();
            encoded = av_packet_alloc();
            if (!codec || !decoded || !encoded) {
                throw std::bad_alloc();
            }
            codec->width = format.width;
            codec->height = format.height;
            if (avcodec_open2(codec, decoder, nullptr) < 0) {
                throw std::runtime_error("Cannot open video decoder");
            }
        }
        if (bytes.size() > INT_MAX) {
            throw std::runtime_error("Video packet is too large");
        }
        av_packet_unref(encoded);
        if (av_new_packet(encoded, static_cast<int>(bytes.size())) < 0) {
            throw std::bad_alloc();
        }
        std::copy(bytes.begin(), bytes.end(), encoded->data);
        encoded->flags = keyframe ? AV_PKT_FLAG_KEY : 0;
        if (avcodec_send_packet(codec, encoded) < 0 || avcodec_receive_frame(codec, decoded) < 0) {
            throw std::runtime_error("Video frame decoding failed");
        }
        if (decoded->width != format.width || decoded->height != format.height) {
            throw std::runtime_error("Video frame dimensions disagree with its description");
        }
        colours = sws_getCachedContext(
            colours, decoded->width, decoded->height, static_cast<AVPixelFormat>(decoded->format),
            decoded->width, decoded->height, AV_PIX_FMT_BGRA, SWS_POINT, nullptr, nullptr, nullptr);
        if (!colours) {
            throw std::runtime_error("Cannot create video colour converter");
        }
        std::uint8_t* output[4] = {frame.pixels.data()};
        const int stride[4] = {static_cast<int>(frame.width * 4)};
        if (sws_scale(colours, decoded->data, decoded->linesize, 0, decoded->height, output,
                      stride) != decoded->height) {
            throw std::runtime_error("Video colour conversion failed");
        }
        av_frame_unref(decoded);
    }
};

Video::Video() : state_(std::make_unique<State>()) {}

Video::~Video() = default;

const Frame& Video::image(const Description& format, std::span<const std::uint8_t> packet) {
    auto& state = *state_;
    state.reset_decoder();
    state.track = nullptr;
    state.previous.reset();
    if (format.codec == "jpeg") {
        state.jpeg(packet);
    } else if (format.codec == "cvid" || format.codec == "rpza") {
        state.compressed_frame(format, packet, true);
    } else {
        throw std::runtime_error("Unsupported picture codec: " + format.codec);
    }
    if (state.frame.width != format.width || state.frame.height != format.height) {
        throw std::runtime_error("Picture dimensions disagree with its description");
    }
    return state.frame;
}

const Frame& Video::decode(const Movie& movie, const Track& track, std::size_t sample) {
    auto& state = *state_;
    const auto& selected = track.samples.at(sample);
    const auto& format = track.descriptions.at(selected.description);
    if (state.track == &track && state.previous == sample) {
        return state.frame;
    }
    if (format.codec == "jpeg") {
        state.reset_decoder();
        state.jpeg(movie.packet(selected));
    } else if (format.codec == "mjpa") {
        state.reset_decoder();
        state.motion_jpeg(format, movie.packet(selected));
    } else if (format.codec == "cvid" || format.codec == "rpza") {
        std::size_t begin = sample;
        if (state.track == &track && state.previous && *state.previous < sample &&
            state.description == selected.description && state.codec) {
            begin = *state.previous + 1;
        } else {
            state.reset_decoder();
            while (begin && !track.samples[begin].keyframe) {
                --begin;
            }
            if (!track.samples[begin].keyframe) {
                throw std::runtime_error("Video seek has no keyframe");
            }
        }
        for (auto index = begin; index <= sample; ++index) {
            const auto& next = track.samples[index];
            if (next.description != selected.description) {
                throw std::runtime_error("Video dependency crosses a codec change");
            }
            state.compressed_frame(format, movie.packet(next), next.keyframe);
        }
    } else {
        throw std::runtime_error("Unsupported video codec: " + format.codec);
    }
    state.track = &track;
    state.previous = sample;
    state.description = selected.description;
    return state.frame;
}

}
