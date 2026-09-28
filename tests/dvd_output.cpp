#include "dvd/output.h"

#include <iostream>
#include <cstring>
#include <stdexcept>
#include <string_view>

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
        const auto before = GetTickCount64();
        output.pause(false);
        Sleep(300);
        const auto actual = output.played();
        const auto expected = static_cast<std::int64_t>(GetTickCount64() - before) * 90 * speed;
        require(actual > expected * 8 / 10 && actual < expected * 12 / 10,
                "XAudio2 consumed PCM at the wrong configured speed");
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
        if (argc == 2 && std::string_view(argv[1]) == "--audio-speed") {
            verify_audio_speed(parent);
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
