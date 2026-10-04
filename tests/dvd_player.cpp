#include "dvd/player.h"
#include "media/tempo.h"
#include <cstring>
#include <vector>

#include <iostream>
#include <stdexcept>
extern "C" {
#include <libavutil/frame.h>
}

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

class Sink final : public dvd::Sink {
public:
    int frames = 0;
    int samples = 0;
    int clears = 0;
    int finishes = 0;
    std::unique_ptr<media::Tempo> tempo;
    std::size_t transformed = 0;
    unsigned speed = 1;
    bool audible = false;
    bool empty = true;
    bool paused = false;
    bool fail = false;
    bool fail_finish = false;
    bool deinterlaced = false;
    int width = 64;
    int height = 48;

    void video(const AVFrame& frame) override {
        require(!fail, "Injected video failure");
        require(frame.width == width && frame.height == height, "Lost source dimensions");
        require(!deinterlaced || !(frame.flags & AV_FRAME_FLAG_INTERLACED),
                "Player presented an unfiltered interlaced frame");
        ++frames;
    }

    void audio(const AVFrame& frame, int first, int count) override {
        require(first >= 0 && count > 0 && first + count <= frame.nb_samples, "Invalid PCM trim");
        samples += count;
        if (tempo) {
            std::vector<std::int16_t> pcm(static_cast<std::size_t>(count) * 2);
            if (frame.format == AV_SAMPLE_FMT_S16) {
                std::memcpy(pcm.data(), frame.extended_data[0] + first * 4, pcm.size() * 2);
            } else {
                const auto* input = reinterpret_cast<const std::int32_t*>(frame.extended_data[0]);
                for (std::size_t i = 0; i < pcm.size(); ++i) {
                    pcm[i] = static_cast<std::int16_t>(input[first * 2 + i] >> 16);
                }
            }
            inspect(tempo->push(pcm));
        }
    }

    void finish_audio() override {
        require(!fail_finish, "Injected audio finish failure");
        ++finishes;
        if (tempo) {
            inspect(tempo->finish());
        }
    }

    std::int64_t audio_horizon() const override {
        return 18000 * speed;
    }

    void inspect(const std::vector<std::int16_t>& pcm) {
        transformed += pcm.size() / 2;
        for (const auto sample : pcm) {
            audible = audible || sample != 0;
        }
    }

    bool drained() override {
        return empty;
    }

    void pause(bool value) override {
        paused = value;
    }

    void clear() override {
        ++clears;
        if (tempo) {
            tempo = std::make_unique<media::Tempo>(48000, 2, speed);
        }
        transformed = 0;
        audible = false;
    }
};

void advance(dvd::Player& player, std::int64_t from, std::int64_t to) {
    for (auto time = from; time <= to; time += 900) {
        player.pump(time);
    }
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        const bool deinterlace = argc > 2 && std::wstring_view(argv[argc - 1]) == L"--bwdif";
        const int arguments = argc - static_cast<int>(deinterlace);
        if (arguments == 3 && std::wstring_view(argv[1]) == L"--media") {
            media::MpegSource source(argv[2]);
            int frames = 0;
            int samples = 0;
            std::int64_t end = 0;
            while (const auto frame = source.next()) {
                if (frame->stream == media::MpegStream::video) {
                    ++frames;
                } else {
                    samples += frame->frame->nb_samples;
                }
                end = std::max(end, frame->time + frame->duration);
            }
            Sink sink;
            sink.width = source.info().width;
            sink.height = source.info().height;
            dvd::Player player(argv[2], sink, deinterlace);
            player.start();
            advance(player, 0, end + 90000);
            std::cout << "frames=" << sink.frames << '/' << frames << " samples=" << sink.samples
                      << '/' << samples << '\n';
            require(sink.frames == frames && sink.samples == samples,
                    "Real DVD output lost decoded content");
            require(player.status() == dvd::Status::completed,
                    "Real DVD playback did not complete");
            return 0;
        }
        require(arguments == 2, "Expected MPEG fixture path");
        Sink sink;
        sink.deinterlaced = deinterlace;
        dvd::Player player(argv[1], sink, deinterlace);
        player.start();
        require(sink.frames == 0 && sink.samples > 0, "Lost initial audio lead");
        player.pump(6005);
        require(sink.frames == 0, "Video presented before its timestamp");
        player.pump(6006);
        require(sink.frames == 1, "First video not presented at its timestamp");
        player.pause();
        const auto samples = sink.samples;
        player.pump(90000);
        require(player.status() == dvd::Status::paused && sink.paused && sink.samples == samples,
                "Pause advanced playback");
        player.resume();
        sink.empty = false;
        advance(player, 6300, 360000);
        std::cout << "frames=" << sink.frames << " samples=" << sink.samples << '\n';
        require(sink.frames == 90 && sink.samples == 144144, "Streaming output lost frames or PCM");
        require(player.status() == dvd::Status::playing, "Decoder EOF completed queued audio");
        require(sink.finishes == 1 && player.audio_finished(), "EOF did not finish PCM once");
        sink.empty = true;
        player.pump(360001);
        require(player.status() == dvd::Status::completed, "Drained playback did not complete");

        sink.frames = sink.samples = 0;
        player.start(90000, 180000);
        advance(player, 90000, 180000);
        require(sink.finishes == 2 && player.audio_finished(), "Finite range did not finish PCM");
        require(sink.samples == 48000, "Seek/range did not trim PCM to one second");
        require(sink.frames >= 29 && sink.frames <= 31, "Seek/range lost video");
        require(player.status() == dvd::Status::completed, "Range did not complete");
        for (const auto speed : {2u, 3u, 4u}) {
            sink.speed = speed;
            sink.tempo = std::make_unique<media::Tempo>(48000, 2, speed);
            sink.frames = sink.samples = 0;
            const auto finishes_before = sink.finishes;
            player.start();
            require(sink.transformed > 0, "Fast startup prebuffer produced no audible PCM");
            advance(player, 0, 360000);
            require(player.status() == dvd::Status::completed &&
                        sink.transformed == 144144 / speed && sink.audible,
                    "Fast decoded EOF lost transformed PCM or its final tail");
            player.pump(360001);
            require(sink.finishes == finishes_before + 1,
                    "Fast decoded EOF finished the processor more than once");
            sink.frames = sink.samples = 0;
            sink.empty = false;
            player.start(90000, 180000);
            advance(player, 90000, 180000);
            require(player.status() == dvd::Status::playing && sink.transformed == 48000 / speed &&
                        sink.audible,
                    "Fast finite range lost PCM or completed before output drained");
            sink.empty = true;
            player.pump(180001);
            require(player.status() == dvd::Status::completed, "Fast finite range did not drain");
            sink.frames = sink.samples = 0;
            player.start(90000, 90090);
            advance(player, 90000, 90900);
            require(player.status() == dvd::Status::completed && sink.samples == 48 &&
                        sink.transformed == 48 / speed,
                    "Short fast finite range did not flush its pending PCM");
        }
        sink.speed = 1;
        sink.tempo.reset();
        player.start();
        player.stop();
        player.pump(900000);
        require(player.status() == dvd::Status::stopped, "Skip became successful EOF");

        player.start();
        sink.fail = true;
        bool failed = false;
        try {
            player.pump(9000);
        } catch (const std::runtime_error&) {
            failed = true;
        }
        require(failed && player.status() == dvd::Status::failed, "Output failure became EOF");
        require(sink.clears >= 6, "Failure/skip did not clear queued output");
        sink.fail = false;
        sink.fail_finish = true;
        failed = false;
        try {
            player.start(90000, 90090);
        } catch (const std::runtime_error&) {
            failed = true;
        }
        require(failed && player.status() == dvd::Status::failed,
                "PCM finish failure became successful EOF");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
