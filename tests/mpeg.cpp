#include "mpeg.h"
#include "mpeg_timing.h"

#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/md5.h>
}

namespace fs = std::filesystem;

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <typename Action> void rejects(Action action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "Invalid MPEG input accepted");
}

std::string hex(const std::array<std::uint8_t, 16>& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : digest) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

struct AudioHash {
    AVMD5* context = av_md5_alloc();

    AudioHash() {
        if (!context) {
            throw std::bad_alloc();
        }
        av_md5_init(context);
    }

    ~AudioHash() {
        av_free(context);
    }

    std::string finish() {
        std::array<std::uint8_t, 16> digest{};
        av_md5_final(context, digest.data());
        return hex(digest);
    }
};

std::string video_hash(const AVFrame* frame) {
    const auto format = static_cast<AVPixelFormat>(frame->format);
    const int size = av_image_get_buffer_size(format, frame->width, frame->height, 1);
    require(size > 0, "Invalid frame buffer size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    require(av_image_copy_to_buffer(bytes.data(), size, frame->data, frame->linesize, format,
                                    frame->width, frame->height, 1) == size,
            "Frame copy failed");
    std::array<std::uint8_t, 16> digest{};
    av_md5_sum(digest.data(), bytes.data(), bytes.size());
    return hex(digest);
}

std::size_t memory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    require(GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters)) != 0,
            "Cannot read process memory");
    return counters.PrivateUsage;
}

struct Summary {
    std::vector<std::string> video;
    std::vector<std::int64_t> times;
    std::int64_t samples = 0;
    std::int64_t first_audio = -1;
    std::int64_t audio_end = 0;
    std::int64_t video_end = 0;
    int estimated = 0;
    int b_frames = 0;
    std::size_t peak = 0;
    std::string audio;
};

Summary decode(media::MpegSource& source, bool print_frames = false) {
    Summary result;
    AudioHash audio;
    std::int64_t last_audio = -1;
    while (const auto output = source.next()) {
        const auto* frame = output->frame;
        require(output->duration > 0, "Frame duration missing");
        result.peak = std::max(result.peak, memory());
        result.estimated += output->estimated_time;
        if (output->stream == media::MpegStream::video) {
            require(result.times.empty() || output->time > result.times.back(),
                    "Video timestamps did not increase");
            if (!print_frames) {
                require((frame->flags & AV_FRAME_FLAG_INTERLACED) &&
                            (frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST),
                        "Field metadata lost");
            }
            result.video.push_back(video_hash(frame));
            result.times.push_back(output->time);
            result.b_frames += frame->pict_type == AV_PICTURE_TYPE_B;
            result.video_end = output->time + output->duration;
            if (print_frames) {
                std::cout << "frame " << output->time << ' ' << output->duration << ' '
                          << !!(frame->flags & AV_FRAME_FLAG_INTERLACED) << ' '
                          << !!(frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) << ' '
                          << frame->repeat_pict << ' ' << frame->sample_aspect_ratio.num << ' '
                          << frame->sample_aspect_ratio.den << ' ' << result.video.back() << '\n';
            }
        } else {
            require(output->time >= last_audio, "Audio timestamps moved backward");
            last_audio = output->time;
            if (result.first_audio < 0) {
                result.first_audio = output->time;
            }
            require(frame->format == AV_SAMPLE_FMT_S16 && frame->ch_layout.nb_channels == 2,
                    "Unexpected DVD PCM format");
            result.samples += frame->nb_samples;
            result.audio_end = output->time + output->duration;
            av_md5_update(audio.context, frame->data[0],
                          static_cast<std::size_t>(frame->nb_samples) * 4);
        }
    }
    require(!source.next(), "EOF was not stable");
    result.audio = audio.finish();
    return result;
}

void timing() {
    media::mpeg::Timeline timeline;
    timeline.reset(6006);
    require(timeline.stamp(AV_NOPTS_VALUE, {1, 90000}, 45000, 3003) == 6006,
            "Missing initial timestamp lost stream offset");
    for (int i = 1; i < 10000; ++i) {
        require(timeline.stamp(AV_NOPTS_VALUE, {1, 90000}, 45000, 3003) == 6006 + i * 3003,
                "Rational frame duration drifted");
    }
    timeline.reset(std::nullopt);
    rejects([&] { timeline.stamp(AV_NOPTS_VALUE, {1, 90000}, 45000, 3003); });
    require(timeline.stamp(51006, {1, 90000}, 45000, 3003) == 6006, "Origin lost");
    require(timeline.stamp(AV_NOPTS_VALUE, {1, 90000}, 45000, 3003) == 9009,
            "Missing final B-frame timestamp was not continued");
    rejects([&] { timeline.stamp(45000, {1, 90000}, 45000, 3003); });
    rejects([&] { timeline.stamp(INT64_MAX, {1, 90000}, 0, 3003); });
}

void fixtures(const fs::path& root) {
    timing();
    const auto movie = root / "dvd-stream.mpg";
    std::ifstream expected(root / "dvd-stream.txt");
    unsigned frames = 0;
    std::int64_t samples = 0;
    std::string audio;
    expected >> frames >> samples >> audio;
    require(expected.good() && frames == 90, "Fixture reference is missing");
    std::vector<std::string> hashes(frames);
    for (auto& hash : hashes) {
        expected >> hash;
    }
    require(!expected.fail(), "Fixture reference is incomplete");
    media::MpegSource source(movie);
    const auto& info = source.info();
    require(info.width == 64 && info.height == 48 && info.frame_rate.num == 30000 &&
                info.frame_rate.den == 1001 && info.sample_aspect_ratio.num == 1 &&
                info.sample_aspect_ratio.den == 1 && info.field_order == AV_FIELD_TT,
            "Video metadata lost");
    require(info.audio_start == 0 && info.video_start == 6006 && info.sample_rate == 48000 &&
                info.channels == 2,
            "A/V origin or audio metadata lost");
    const auto result = decode(source);
    require(result.video == hashes && result.samples == samples && result.audio == audio,
            "Sequential decode differs from FFmpeg reference, including decoder drain");
    require(result.b_frames > 0 && result.times.front() == 6006 && result.first_audio == 0,
            "B-frame or stream timing fixture is ineffective");
    require(result.video_end == 6006 + frames * 3003, "Video EOF time is incorrect");
    source.seek(180180);
    const auto middle = decode(source);
    require(!middle.times.empty() && middle.times.front() > result.times.front() &&
                middle.times.front() <= 180180 && middle.times.back() == result.times.back(),
            "Seek preroll or EOF is incorrect");
    for (std::size_t i = 0; i < middle.times.size(); ++i) {
        const auto found = std::find(result.times.begin(), result.times.end(), middle.times[i]);
        require(found != result.times.end() &&
                    middle.video[i] ==
                        hashes[static_cast<std::size_t>(found - result.times.begin())],
                "Seek returned stale decoder frames");
    }
    source.seek(0);
    const auto restart = decode(source);
    require(restart.video == hashes && restart.times == result.times && restart.audio == audio &&
                restart.samples == samples && restart.first_audio == 0,
            "Backward seek did not reproduce the complete clip");
    for (int i = 0; i < 12; ++i) {
        source.seek(180180);
        for (int j = 0; j < 4; ++j) {
            require(source.next().has_value(), "Seek during decode failed");
        }
    }

    const auto temporary =
        fs::temp_directory_path() / (L"xfiles-mpeg-" + std::to_wstring(GetCurrentProcessId()));
    require(fs::create_directory(temporary), "Cannot create fixture directory");

    struct Cleanup {
        fs::path path;

        ~Cleanup() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{temporary};

    const auto invalid = temporary / "invalid.mpg";
    std::ofstream(invalid, std::ios::binary) << "not an MPEG program stream";
    rejects([&] { media::MpegSource bad(invalid); });
    rejects([&] { media::MpegSource bad(temporary); });
    rejects([&] { media::MpegSource bad(temporary / "missing"); });
    rejects([&] { source.seek(-1); });
    rejects([&] { source.next(); });

    std::ifstream input(movie, std::ios::binary);
    const std::vector<std::uint8_t> original{std::istreambuf_iterator<char>(input),
                                             std::istreambuf_iterator<char>()};
    auto damaged = original;
    bool changed = false;
    for (std::size_t i = 0; i + 5 < damaged.size(); ++i) {
        if (damaged[i] == 0 && damaged[i + 1] == 0 && damaged[i + 2] == 1 && damaged[i + 3] == 1) {
            damaged[i + 4] &= 7;
            changed = true;
        }
    }
    require(changed, "Corrupt fixture has no slices");
    const auto corrupt = temporary / "corrupt.mpg";
    {
        std::ofstream output(corrupt, std::ios::binary);
        output.write(reinterpret_cast<const char*>(damaged.data()), damaged.size());
    }
    rejects([&] {
        media::MpegSource bad(corrupt);
        while (bad.next()) {
        }
    });

    auto untimed = original;
    int video_packets = 0;
    for (std::size_t i = 0; i + 19 < untimed.size(); ++i) {
        if (untimed[i] == 0 && untimed[i + 1] == 0 && untimed[i + 2] == 1 &&
            untimed[i + 3] == 0xe0 && (untimed[i + 6] & 0xc0) == 0x80) {
            if (video_packets++ == 0) {
                continue;
            }
            const int bytes = (untimed[i + 7] & 0xc0) == 0xc0   ? 10
                              : (untimed[i + 7] & 0xc0) == 0x80 ? 5
                                                                : 0;
            untimed[i + 7] &= 0x3f;
            std::fill_n(untimed.begin() + i + 9, bytes, std::uint8_t{0xff});
        }
    }
    require(video_packets > 2, "Missing timestamp fixture has too few packets");
    const auto missing_pts = temporary / "missing-pts.mpg";
    {
        std::ofstream output(missing_pts, std::ios::binary);
        output.write(reinterpret_cast<const char*>(untimed.data()), untimed.size());
    }
    media::MpegSource missing(missing_pts);
    const auto inferred = decode(missing);
    require(inferred.video == hashes && inferred.estimated > 0 && inferred.times == result.times,
            "Missing timestamps changed decoded frames or their timeline");

    const auto padded = temporary / L"stream-\u65e5.mpg";
    fs::copy_file(movie, padded);
    {
        std::ofstream output(padded, std::ios::binary | std::ios::app);
        const std::array<char, 6> header{
            0, 0, 1, static_cast<char>(0xbe), static_cast<char>(0xff), static_cast<char>(0xff)};
        const std::string padding(65535, static_cast<char>(0xff));
        for (int i = 0; i < 1024; ++i) {
            output.write(header.data(), header.size());
            output.write(padding.data(), padding.size());
        }
        require(output.good(), "Cannot write padded fixture");
    }
    const auto before = memory();
    media::MpegSource large(padded);
    const auto expanded = decode(large);
    require(expanded.video == hashes && expanded.audio == audio, "Padding changed decoded output");
    require(expanded.peak < before + 24 * 1024 * 1024, "Streaming memory scales with file size");
    std::cout
        << "MPEG fixtures passed: B-frames, PCM, timing, seek, EOF, errors, Unicode, memory\n";
}

}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--inspect") {
            const auto before = memory();
            media::MpegSource source(argv[2]);
            const auto& info = source.info();
            std::cout << "info " << info.width << ' ' << info.height << ' ' << info.frame_rate.num
                      << '/' << info.frame_rate.den << ' ' << info.sample_aspect_ratio.num << '/'
                      << info.sample_aspect_ratio.den << ' ' << info.video_start << ' '
                      << info.audio_start << '\n';
            const auto result = decode(source, true);
            std::cout << "summary " << result.video.size() << ' ' << result.samples << ' '
                      << result.audio << ' ' << result.video_end << ' ' << result.audio_end << ' '
                      << result.estimated << ' '
                      << (result.peak > before ? result.peak - before : 0) << '\n';
        } else {
            require(argc == 2, "Usage: mpeg-test <fixture directory> | --inspect <movie>");
            fixtures(argv[1]);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
