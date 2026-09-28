#include "mpeg.h"
#include "mpeg_filter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/md5.h>
}

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct FrameDeleter {
    void operator()(AVFrame* frame) const {
        av_frame_free(&frame);
    }
};

using Frame = std::unique_ptr<AVFrame, FrameDeleter>;

Frame make_frame(int index, bool interlaced, bool top_first) {
    Frame frame(av_frame_alloc());
    require(!!frame, "Cannot allocate synthetic frame");
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = 64;
    frame->height = 48;
    frame->sample_aspect_ratio = {4, 3};
    frame->color_range = AVCOL_RANGE_MPEG;
    frame->colorspace = AVCOL_SPC_BT470BG;
    frame->chroma_location = AVCHROMA_LOC_LEFT;
    frame->flags = (interlaced ? AV_FRAME_FLAG_INTERLACED : 0) |
                   (top_first ? AV_FRAME_FLAG_TOP_FIELD_FIRST : 0);
    require(av_frame_get_buffer(frame.get(), 32) >= 0, "Cannot allocate synthetic pixels");
    for (int y = 0; y < frame->height; ++y) {
        for (int x = 0; x < frame->width; ++x) {
            frame->data[0][y * frame->linesize[0] + x] = static_cast<std::uint8_t>(
                interlaced ? ((y & 1) ? 170 + (x + index * 7) % 30 : 35 + (x + index * 13) % 30)
                           : 30 + (x + y + index * 11) % 180);
        }
    }
    for (int plane = 1; plane < 3; ++plane) {
        for (int y = 0; y < frame->height / 2; ++y) {
            std::fill_n(frame->data[plane] + y * frame->linesize[plane], frame->width / 2,
                        std::uint8_t{128});
        }
    }
    return frame;
}

std::vector<std::uint8_t> pixels(const AVFrame& frame) {
    require(frame.format == AV_PIX_FMT_YUV420P && frame.width == 64 && frame.height == 48,
            "BWDIF changed the synthetic frame format");
    std::vector<std::uint8_t> result;
    result.reserve(64 * 48 * 3 / 2);
    for (int plane = 0; plane < 3; ++plane) {
        const int width = plane == 0 ? 64 : 32;
        const int height = plane == 0 ? 48 : 24;
        for (int y = 0; y < height; ++y) {
            result.insert(result.end(), frame.data[plane] + y * frame.linesize[plane],
                          frame.data[plane] + y * frame.linesize[plane] + width);
        }
    }
    return result;
}

struct Filtered {
    std::vector<std::uint8_t> pixels;
    std::int64_t time;
    std::int64_t duration;
    bool estimated;
    bool interlaced;
};

void collect(media::mpeg::BwdifFilter& filter, std::vector<Filtered>& output) {
    Frame held;
    std::vector<std::uint8_t> held_pixels;
    for (;;) {
        const auto frame = filter.pull();
        if (held) {
            require(pixels(*held) == held_pixels, "A cloned BWDIF output expired on the next pull");
        }
        if (!frame) {
            return;
        }
        require(frame->frame->sample_aspect_ratio.num == 4 &&
                    frame->frame->sample_aspect_ratio.den == 3 &&
                    frame->frame->color_range == AVCOL_RANGE_MPEG &&
                    frame->frame->colorspace == AVCOL_SPC_BT470BG &&
                    frame->frame->chroma_location == AVCHROMA_LOC_LEFT,
                "BWDIF changed video color or aspect metadata");
        held.reset(av_frame_clone(frame->frame));
        require(!!held, "Cannot clone BWDIF output");
        held_pixels = pixels(*frame->frame);
        output.push_back({held_pixels, frame->time, frame->duration, frame->estimated_time,
                          !!(frame->frame->flags & AV_FRAME_FLAG_INTERLACED)});
    }
}

std::vector<Filtered> synthetic(bool top_first, bool mixed, int count,
                                bool alternate_order = false) {
    media::mpeg::BwdifFilter filter({30000, 1001});
    std::vector<Filtered> output;
    for (int i = 0; i < count; ++i) {
        auto frame = make_frame(i, !mixed || i != 1, top_first != (alternate_order && (i & 1)));
        filter.push({media::MpegStream::video, frame.get(), 6006 + i * 3003, 3003 + i, i == 1});
        collect(filter, output);
    }
    filter.finish();
    collect(filter, output);
    require(!filter.pull(), "BWDIF EOF was not stable");
    require(output.size() == static_cast<std::size_t>(count),
            "BWDIF did not produce one frame per input");
    for (int i = 0; i < count; ++i) {
        require(output[i].time == 6006 + i * 3003 && output[i].duration == 3003 + i &&
                    output[i].estimated == (i == 1),
                "BWDIF changed source frame metadata");
    }
    filter.reset();
    auto restart = make_frame(0, true, top_first);
    filter.push({media::MpegStream::video, restart.get(), 6006, 3003, false});
    filter.finish();
    std::vector<Filtered> repeated;
    collect(filter, repeated);
    require(repeated.size() == 1 && repeated.front().time == 6006,
            "BWDIF reset retained prior metadata");
    filter.reset();
    filter.push({media::MpegStream::video, restart.get(), 6006, 3003, false});
    filter.finish();
    std::vector<Filtered> second;
    collect(filter, second);
    require(second.size() == 1 && second.front().pixels == repeated.front().pixels,
            "BWDIF reset retained prior pixels");
    filter.reset();
    auto pending = make_frame(1, true, top_first);
    filter.push({media::MpegStream::video, restart.get(), 6006, 3003, false});
    filter.push({media::MpegStream::video, pending.get(), 9009, 3003, true});
    filter.reset();
    filter.push({media::MpegStream::video, restart.get(), 6006, 3003, false});
    filter.finish();
    std::vector<Filtered> discarded;
    collect(filter, discarded);
    require(discarded.size() == 1 && discarded.front().pixels == repeated.front().pixels,
            "BWDIF reset retained pending lookahead");
    return output;
}

void telecine_neighbor() {
    media::mpeg::BwdifFilter filter({30000, 1001});
    constexpr std::array<std::int64_t, 5> times{6006, 9009, 13513, 19519, 22522};
    constexpr std::array<std::int64_t, 5> durations{3003, 4504, 6006, 3003, 3003};
    std::array<std::vector<std::uint8_t>, 5> originals;
    std::vector<Filtered> output;
    for (int i = 0; i < 5; ++i) {
        auto frame = make_frame(i, i != 2, true);
        frame->repeat_pict = i == 2 ? 1 : 0;
        originals[i] = pixels(*frame);
        filter.push({media::MpegStream::video, frame.get(), times[i], durations[i], i == 3});
        collect(filter, output);
    }
    filter.finish();
    collect(filter, output);
    require(output.size() == 5, "Telecine neighbor changed BWDIF frame count");
    for (int i = 0; i < 5; ++i) {
        require(output[i].time == times[i] && output[i].duration == durations[i] &&
                    output[i].estimated == (i == 3),
                "Telecine neighbor changed single-rate frame metadata");
    }
    for (const int neighbor : {1, 3}) {
        require(output[neighbor].pixels == originals[neighbor] && output[neighbor].interlaced,
                "BWDIF did not preserve an interlaced repeat neighbor");
    }
    require(output[2].pixels == originals[2] && !output[2].interlaced,
            "BWDIF modified a repeated progressive frame");
}

struct Stamp {
    std::int64_t time;
    std::int64_t duration;
    bool estimated;

    bool operator==(const Stamp&) const = default;
};

struct Capture {
    std::vector<Stamp> video;
    std::vector<Stamp> audio;
    std::vector<std::array<std::uint8_t, 16>> images;
    std::array<std::uint8_t, 16> audio_digest{};
    std::int64_t samples = 0;
};

Capture capture(media::MpegSource& source) {
    Capture result;
    std::unique_ptr<AVMD5, decltype(&av_free)> digest(av_md5_alloc(), av_free);
    require(!!digest, "Cannot allocate audio digest");
    av_md5_init(digest.get());
    while (const auto output = source.next()) {
        const Stamp stamp{output->time, output->duration, output->estimated_time};
        if (output->stream == media::MpegStream::video) {
            result.video.push_back(stamp);
            std::array<std::uint8_t, 16> image{};
            const auto bytes = pixels(*output->frame);
            av_md5_sum(image.data(), bytes.data(), bytes.size());
            result.images.push_back(image);
        } else {
            result.audio.push_back(stamp);
            result.samples += output->frame->nb_samples;
            av_md5_update(digest.get(), output->frame->data[0],
                          static_cast<std::size_t>(output->frame->nb_samples) * 4);
        }
    }
    av_md5_final(digest.get(), result.audio_digest.data());
    require(!source.next(), "MPEG EOF was not stable");
    return result;
}

std::string hex(const std::array<std::uint8_t, 16>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : bytes) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

void source_fixture(const std::filesystem::path& root) {
    const auto movie = root / "dvd-stream.mpg";
    media::MpegSource raw(movie);
    media::MpegSource filtered(movie, true);
    const auto original = capture(raw);
    const auto enhanced = capture(filtered);
    require(original.video == enhanced.video && original.audio == enhanced.audio &&
                original.audio_digest == enhanced.audio_digest &&
                original.samples == enhanced.samples,
            "BWDIF changed source timing or audio bytes");
    require(original.video.size() == 90 && original.images != enhanced.images,
            "BWDIF did not process the DVD fixture");
    std::ifstream reference(root / "dvd-stream-bwdif.txt");
    require(!!reference, "Missing independent BWDIF frame reference");
    for (const auto& image : enhanced.images) {
        std::string expected;
        require(bool(reference >> expected) && hex(image) == expected,
                "BWDIF pixels differ from FFmpeg reference");
    }
    std::string extra;
    require(!(reference >> extra), "BWDIF reference has extra frames");
    filtered.seek(180180);
    const auto suffix = capture(filtered);
    require(!suffix.video.empty() && suffix.video.front().time <= 180180 &&
                suffix.video.back() == enhanced.video.back(),
            "Filtered seek lost DVD preroll or final frame");
    for (std::size_t i = 0; i < suffix.video.size(); ++i) {
        if (suffix.video[i].time < 180180) {
            continue;
        }
        const auto found = std::find(enhanced.video.begin(), enhanced.video.end(), suffix.video[i]);
        require(found != enhanced.video.end() &&
                    suffix.images[i] == enhanced.images[found - enhanced.video.begin()],
                "Filtered seek differs from sequential decode");
    }
    filtered.seek(0);
    const auto restart = capture(filtered);
    require(restart.video == enhanced.video && restart.audio == enhanced.audio &&
                restart.images == enhanced.images && restart.audio_digest == enhanced.audio_digest,
            "Filtered reopen did not reproduce the complete clip");
}

}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: mpeg-filter-test <fixture directory>");
        const auto top = synthetic(true, true, 3);
        const auto bottom = synthetic(false, true, 3);
        const auto progressive = make_frame(1, false, true);
        require(top[1].pixels == pixels(*progressive) && bottom[1].pixels == pixels(*progressive),
                "BWDIF modified a progressive frame");
        require(top[0].pixels != bottom[0].pixels || top[2].pixels != bottom[2].pixels,
                "BWDIF ignored field order");
        require(top[0].pixels != pixels(*make_frame(0, true, true)) ||
                    top[2].pixels != pixels(*make_frame(2, true, true)),
                "BWDIF did not deinterlace interlaced frames");
        synthetic(true, false, 1);
        telecine_neighbor();
        const auto alternating = synthetic(true, false, 4, true);
        const auto constant = synthetic(true, false, 4);
        require(alternating[1].pixels != constant[1].pixels ||
                    alternating[3].pixels != constant[3].pixels,
                "BWDIF did not follow per-frame field order");
        for (int i = 0; i < 4; ++i) {
            const int first_field = i & 1;
            const auto input = make_frame(i, true, !first_field);
            const auto original = pixels(*input);
            for (int y = first_field; y < 48; y += 2) {
                const auto offset = static_cast<std::size_t>(y * 64);
                require(std::equal(alternating[i].pixels.begin() + offset,
                                   alternating[i].pixels.begin() + offset + 64,
                                   original.begin() + offset),
                        "BWDIF lost the selected source field");
            }
        }
        source_fixture(argv[1]);
        std::cout
            << "BWDIF frames, fields, progressive bypass, drain, seek, timing, audio passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
