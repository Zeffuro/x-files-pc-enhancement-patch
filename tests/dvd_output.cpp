#include "dvd/output.h"
#include "dvd/player.h"
#include "dvd/clock.h"

#include <iostream>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

extern "C" {
#include <libavutil/frame.h>
}

namespace {

UINT frame_message = RegisterWindowMessageW(L"XFilesEnhancement.DvdFrame");
bool reject = false;
int presented = 0;
COLORREF center = 0;
COLORREF edge = 0;
COLORREF margin = 0;
COLORREF odd_line = 0;
bool inspect_caption = false;
int inside_caption = 0;
int below_caption = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

LRESULT CALLBACK messages(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == frame_message) {
        if (!wparam) {
            return MAKELONG(640, 480);
        }
        if (reject || lparam != MAKELPARAM(640, 480)) {
            return 0;
        }
        auto dc = reinterpret_cast<HDC>(wparam);
        center = GetPixel(dc, 320, 240);
        edge = GetPixel(dc, 635, 240);
        margin = GetPixel(dc, 320, 10);
        odd_line = GetPixel(dc, 320, 239);
        if (inspect_caption) {
            inside_caption = below_caption = 0;
            for (int y = 300; y < 420; ++y) {
                for (int x = 200; x < 440; ++x) {
                    const auto color = GetPixel(dc, x, y);
                    if (GetRValue(color) > 220 && GetGValue(color) > 220 &&
                        GetBValue(color) > 220) {
                        ++(y < 360 ? inside_caption : below_caption);
                    }
                }
            }
        }
        ++presented;
        return 1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

class StreamingSink final : public dvd::Sink {
public:
    StreamingSink(HWND parent, unsigned multiplier) : output(parent), speed(multiplier) {}

    void video(const AVFrame& frame) override {
        output.video(frame);
        ++frames;
    }

    void audio(const AVFrame& frame, int first, int count) override {
        output.audio(frame, first, count);
        samples += count;
    }

    void finish_audio() override {
        output.finish_audio();
        ++finishes;
    }

    std::int64_t audio_horizon() const override {
        return 18000 * speed;
    }

    bool drained() override {
        return output.drained();
    }

    void pause(bool paused) override {
        output.pause(paused);
    }

    void clear() override {
        output.clear();
        frames = samples = finishes = 0;
    }

    dvd::Output output;
    unsigned speed;
    int frames = 0;
    int samples = 0;
    int finishes = 0;
};

void verify_streaming_audio(HWND parent, const std::filesystem::path& fixture, unsigned speed,
                            bool actual_clip = false) {
    StreamingSink sink(parent, speed);
    dvd::Player player(fixture, sink);
    const auto info = player.info();
    sink.output.rectangles({0, 0, info.width, info.height}, {0, 0, 640, 480}, {0, 0, 640, 480});
    sink.output.speed(speed);
    for (const auto range :
         {std::pair<std::int64_t, std::int64_t>{0, actual_clip ? 450000 : INT64_MAX},
          {90000, 180000},
          {90000, 90090}}) {
        struct DecodedSink final : dvd::Sink {
            int frames = 0;
            std::int64_t samples = 0;

            void video(const AVFrame&) override {
                ++frames;
            }

            void audio(const AVFrame&, int, int count) override {
                samples += count;
            }

            void finish_audio() override {}

            bool drained() override {
                return true;
            }

            void pause(bool) override {}

            void clear() override {
                frames = 0;
                samples = 0;
            }
        } decoded;

        if (actual_clip) {
            dvd::Player original(fixture, decoded);
            original.start(range.first, range.second);
            for (auto time = range.first;
                 original.status() == dvd::Status::playing && time <= range.second + 90000;
                 time += 9000) {
                original.pump(time);
            }
            require(original.status() == dvd::Status::completed,
                    "Original DVD range did not decode");
        }
        player.start(range.first, range.second);
        require(player.audio_finished() || !sink.drained(),
                "Streaming startup retained all PCM beyond the prebuffer");
        dvd::Clock clock;
        const auto begin = GetTickCount64();
        clock.reset(range.first, begin);
        bool tail = false;
        bool pause_checked = false;
        ULONGLONG pause_time = 0;
        while (player.status() == dvd::Status::playing && GetTickCount64() - begin < 5000) {
            const auto now = GetTickCount64();
            const auto played = sink.output.played();
            player.pump(clock.advance(now, played, speed, false, tail));
            const bool drained = sink.drained();
            require(player.audio_finished() || !drained,
                    "Streaming audio exhausted its queue before decoder EOF");
            tail = player.audio_finished() && drained;
            if (range.first == 0 && !pause_checked && played >= 45000) {
                player.pause();
                const auto paused_samples = sink.output.played();
                const auto pause_begin = GetTickCount64();
                Sleep(100);
                require(sink.output.played() - paused_samples <= 1800,
                        "Paused streaming output continued consuming PCM");
                pause_time = GetTickCount64() - pause_begin;
                clock.anchor(GetTickCount64());
                player.resume();
                pause_checked = true;
            }
            Sleep(10);
        }
        require(player.status() == dvd::Status::completed && sink.finishes == 1 && sink.drained(),
                "Streaming EOF/range did not finish and drain exactly once");
        const auto represented = static_cast<std::int64_t>(sink.samples / speed) * speed;
        require(sink.output.played() == represented * 90000 / 48000,
                "Streaming consumed output did not map to source samples");
        if (actual_clip) {
            require(sink.samples == decoded.samples && sink.frames == decoded.frames,
                    "Actual DVD range differs from its original decoded content");
        }
        if (range.first == 0) {
            std::cout << speed << "x streaming frames=" << sink.frames
                      << " source_samples=" << sink.samples
                      << " played_ticks=" << sink.output.played() << '\n';
            require(
                (actual_clip ? sink.frames > 100 : sink.frames == 90 && sink.samples == 144144) &&
                    pause_checked,
                "Streaming output lost content or skipped pause/resume");
            const auto elapsed = GetTickCount64() - begin - pause_time;
            const auto duration = static_cast<ULONGLONG>(actual_clip ? 5000 : 3003) / speed;
            require(elapsed > duration * 8 / 10 && elapsed < duration + 600,
                    "Streaming playback did not advance at the selected speed");
        } else if (!actual_clip) {
            require(sink.samples == (range.second == 180000 ? 48000 : 48),
                    "Streaming finite range submitted the wrong source samples");
        }
    }
    std::cout << "Muted production DVD" << speed
              << "x streaming startup, EOF, finite ranges and pause passed\n";
}

void verify_audio_speed(HWND parent) {
    auto* frame = av_frame_alloc();
    require(frame != nullptr, "Cannot allocate silent speed fixture");

    struct FreeFrame {
        AVFrame*& frame;

        ~FreeFrame() {
            av_frame_free(&frame);
        }
    } cleanup{frame};

    frame->format = AV_SAMPLE_FMT_S16;
    frame->sample_rate = 48000;
    frame->nb_samples = 96000;
    av_channel_layout_default(&frame->ch_layout, 2);
    require(av_frame_get_buffer(frame, 0) == 0, "Cannot allocate silent PCM");
    std::memset(frame->data[0], 0, static_cast<std::size_t>(frame->nb_samples) * 4);
    dvd::Output output(parent);
    for (const unsigned speed : {2, 3, 4, 1}) {
        output.pause(true);
        output.discard_audio();
        output.speed(speed);
        output.audio(*frame, 0, frame->nb_samples);
        output.finish_audio();
        output.finish_audio();
        const auto before = GetTickCount64();
        output.pause(false);
        Sleep(300);
        const auto actual = output.played();
        const auto expected = static_cast<std::int64_t>(GetTickCount64() - before) * 90 * speed;
        require(actual > expected * 8 / 10 && actual < expected * 12 / 10,
                "XAudio2 consumed PCM at the wrong configured speed");
        const auto deadline = GetTickCount64() + 3000;
        while (!output.drained() && GetTickCount64() < deadline) {
            Sleep(10);
        }
        require(output.drained() && output.played() == 180000,
                "DVD audio finish lost samples or mapped output frames incorrectly");
    }
    for (const int format : {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S32}) {
        av_frame_unref(frame);
        frame->format = format;
        frame->sample_rate = 48000;
        frame->nb_samples = 3001;
        av_channel_layout_default(&frame->ch_layout, 2);
        require(av_frame_get_buffer(frame, 0) == 0, "Cannot allocate short PCM");
        std::memset(frame->data[0], 0,
                    static_cast<std::size_t>(frame->nb_samples) *
                        (format == AV_SAMPLE_FMT_S16 ? 4 : 8));
        for (unsigned speed = 2; speed <= 4; ++speed) {
            output.pause(true);
            output.discard_audio();
            output.speed(speed);
            output.audio(*frame, 0, frame->nb_samples);
            require(output.played() == 0, "Audio reset retained its prior consumed samples");
            output.finish_audio();
            output.pause(false);
            const auto deadline = GetTickCount64() + 1000;
            while (!output.drained() && GetTickCount64() < deadline) {
                Sleep(10);
            }
            const auto represented = frame->nb_samples / speed * speed;
            require(output.drained() && output.played() == represented * 90000LL / 48000,
                    "Short S16/S32 finish lost the bounded tail or frame trim");
        }
    }
    std::cout << "Silent XAudio2 source sample rates at2x/3x/4x and restored1x passed\n";
}

}

int main(int argc, char** argv) {
    HWND parent = nullptr;
    AVFrame* frame = nullptr;
    try {
        WNDCLASSW type{};
        type.lpfnWndProc = messages;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"XFilesDvdOutputTest";
        require(RegisterClassW(&type) != 0, "Cannot register DVD output test");
        parent = CreateWindowW(type.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 1280, 960,
                               nullptr, nullptr, type.hInstance, nullptr);
        require(parent != nullptr, "Cannot create DVD output test parent");
        if (argc >= 2 && (std::string_view(argv[1]) == "--audio-speed" ||
                          std::string_view(argv[1]) == "--audio-clip")) {
            require(argc == 3, "Expected MPEG fixture path for streaming audio checks");
            SetEnvironmentVariableW(L"XFILES_TEST_MUTE_AUDIO", L"1");
            const bool actual_clip = std::string_view(argv[1]) == "--audio-clip";
            if (!actual_clip) {
                verify_audio_speed(parent);
            }
            for (unsigned speed = 2; speed <= 4; ++speed) {
                verify_streaming_audio(parent, argv[2], speed, actual_clip);
            }
            DestroyWindow(parent);
            return 0;
        }
        frame = av_frame_alloc();
        require(frame != nullptr, "Cannot allocate DVD output test frame");
        frame->format = AV_PIX_FMT_BGRA;
        frame->width = 720;
        frame->height = 480;
        frame->color_range = AVCOL_RANGE_JPEG;
        require(av_frame_get_buffer(frame, 0) == 0, "Cannot allocate DVD output pixels");
        for (int y = 0; y < frame->height; ++y) {
            for (int x = 0; x < frame->width; ++x) {
                auto* pixel = frame->data[0] + y * frame->linesize[0] + x * 4;
                pixel[0] = 0;
                pixel[1] = x < 704 ? 255 : 0;
                pixel[2] = x < 704 ? 0 : 255;
                pixel[3] = 255;
            }
        }
        for (int run = 0; run < 2; ++run) {
            dvd::Output output(parent);
            output.video(*frame);
            const auto before = presented;
            output.show(true);
            require(presented == before + 1, "Show did not present the retained DVD frame");
            require(center == RGB(0, 255, 0) && edge == RGB(0, 255, 0),
                    "DVD source crop or full-frame composition changed");
            RECT size{};
            require(GetClientRect(output.window(), &size) && size.right == 0 && size.bottom == 0,
                    "DVD child can trigger legacy wrapper painting");
            require(!(GetWindowLongW(output.window(), GWL_STYLE) & WS_VISIBLE),
                    "Shared DVD renderer showed a competing child");
            inspect_caption = true;
            CaptionStyle style;
            style.font = CaptionFont::Modern;
            output.caption(L"DVD caption", style);
            require(inside_caption > 10 && below_caption == 0,
                    "DVD caption did not use the native picture area");
            const auto before_audio_reset = presented;
            output.discard_audio();
            require(presented == before_audio_reset && inside_caption > 10,
                    "Speed audio rebase cleared the retained movie or caption");
            require(center == RGB(0, 255, 0) && edge == RGB(0, 255, 0),
                    "DVD caption changed the source crop");
            style.placement = CaptionPlacement::Below;
            output.caption(L"DVD caption", style);
            require(inside_caption == 0 && below_caption > 10,
                    "DVD caption placement did not clear its prior position");
            output.caption({}, style);
            require(inside_caption == 0 && below_caption == 0,
                    "Caption end or disable retained stale text");
            output.caption(L"DVD caption", style);
            output.clear();
            require(inside_caption == 0 && below_caption == 0,
                    "DVD stop/seek clear retained stale caption text");
            inspect_caption = false;
            output.video(*frame);
            output.rectangles({0, 0, 704, 480}, {0, 60, 640, 420}, {0, 0, 640, 480});
            require(center == RGB(0, 255, 0) && margin == RGB(0, 0, 0),
                    "DVD picture and black margins were not composed together");
            output.show(false);
            const auto hidden = presented;
            output.clear();
            require(presented == hidden, "Hidden DVD output changed the game surface");
            output.show(true);
            require(center == RGB(0, 0, 0), "Cleared DVD output retained stale pixels");
            reject = true;
            bool failed = false;
            try {
                output.video(*frame);
            } catch (const std::runtime_error&) {
                failed = true;
            }
            reject = false;
            require(failed, "Failed DVD presentation was silently accepted");
        }
        for (int y = 0; y < frame->height; ++y) {
            for (int x = 0; x < frame->width; ++x) {
                auto* pixel = frame->data[0] + y * frame->linesize[0] + x * 4;
                pixel[0] = 0;
                pixel[1] = y % 2 == 0 ? static_cast<std::uint8_t>(y / 2) : 0;
                pixel[2] = y % 2 == 0 ? 0 : 200;
            }
        }
        for (const int flags : {0, AV_FRAME_FLAG_INTERLACED,
                                AV_FRAME_FLAG_INTERLACED | AV_FRAME_FLAG_TOP_FIELD_FIRST}) {
            dvd::Output output(parent);
            output.show(true);
            frame->flags = flags;
            output.video(*frame);
            require(odd_line == RGB(200, 0, 0) && center == RGB(0, 120, 0),
                    "DVD output altered decoded field rows");
        }
        av_frame_free(&frame);
        DestroyWindow(parent);
        std::cout << "DVD output composition, ownership, reopen and failure checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        av_frame_free(&frame);
        if (parent) {
            DestroyWindow(parent);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
