#include "output.h"
#include "canvas.h"
#include "playback/caption_paint.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <xaudio2.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace dvd {
namespace {

void check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw std::runtime_error(std::string(operation) + " failed (" +
                                 std::to_string(static_cast<unsigned long>(result)) + ")");
    }
}

void check(int result, const char* operation) {
    if (result < 0) {
        throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(result) +
                                 ")");
    }
}

int colour_space(AVColorSpace space) {
    switch (space) {
        case AVCOL_SPC_BT709:
            return SWS_CS_ITU709;
        case AVCOL_SPC_FCC:
            return SWS_CS_FCC;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:
            return SWS_CS_BT2020;
        default:
            return SWS_CS_ITU601;
    }
}

constexpr int max_width = 720;
constexpr int max_height = 576;
constexpr std::size_t max_audio_bytes = 4 * 1024 * 1024;
constexpr std::size_t max_audio_buffers = 48;

SIZE physical_client_size(HWND window) {
    WINDOWINFO info{sizeof(info)};
    if (!GetWindowInfo(window, &info)) {
        throw std::runtime_error("Could not measure DVD output parent");
    }
    const auto width = static_cast<std::int64_t>(info.rcClient.right) - info.rcClient.left;
    const auto height = static_cast<std::int64_t>(info.rcClient.bottom) - info.rcClient.top;
    if (width < 0 || height < 0 || width > 32767 || height > 32767) {
        throw std::runtime_error("Invalid DVD output parent size");
    }
    return {static_cast<LONG>(width), static_cast<LONG>(height)};
}

struct AudioCallback final : IXAudio2VoiceCallback {
    std::atomic<std::uint64_t> completed{0};
    std::atomic<HRESULT> error{S_OK};

    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}

    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}

    void STDMETHODCALLTYPE OnStreamEnd() override {}

    void STDMETHODCALLTYPE OnBufferStart(void*) override {}

    void STDMETHODCALLTYPE OnBufferEnd(void*) override {
        completed.fetch_add(1, std::memory_order_release);
    }

    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}

    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT result) override {
        error.store(result, std::memory_order_release);
    }
};

}

struct Output::State {
    explicit State(HWND parent);
    ~State();

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void layout();
    void paint(HDC dc);
    void present();
    void ensure_audio();
    void reclaim();
    void reset_audio() noexcept;

    HWND parent = nullptr;
    HWND window = nullptr;
    WNDPROC original_proc = nullptr;
    UINT frame_message = RegisterWindowMessageW(L"XFilesEnhancement.DvdFrame");
    bool shared_renderer = false;
    bool visible = false;
    std::unique_ptr<Canvas> canvas;
    RECT source{0, 0, 704, 480};
    RECT dest{0, 0, 640, 480};
    RECT client{0, 0, 640, 480};
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    SwsContext* scaler = nullptr;
    SwrContext* resampler = nullptr;
    Microsoft::WRL::ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* voice = nullptr;
    AudioCallback callback;
    std::deque<std::unique_ptr<std::vector<std::uint8_t>>> buffers;
    std::size_t queued_bytes = 0;
    std::uint64_t reclaimed = 0;
    bool audio_apartment = false;
    bool paused = false;
    unsigned speed = 1;
    std::wstring caption;
    CaptionStyle caption_style;
};

Output::State::State(HWND owner) : parent(owner) {
    if (!IsWindow(owner)) {
        throw std::runtime_error("DVD output parent is not a window");
    }
    shared_renderer = SendMessageW(parent, frame_message, 0, 0) != 0;
    if (!shared_renderer &&
        SendMessageW(parent, RegisterWindowMessageW(L"XFilesEnhancement.DisplayMode"), 0, 0)) {
        throw std::runtime_error("DVD playback requires the matching display wrapper");
    }
    const auto size = shared_renderer ? SIZE{640, 480} : physical_client_size(owner);
    canvas = std::make_unique<Canvas>(std::max(1L, size.cx), std::max(1L, size.cy));
    // Even hidden children enter cnc-ddraw's legacy scan unless their size is zero.
    window = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0,
                             shared_renderer ? 0 : size.cx, shared_renderer ? 0 : size.cy, owner,
                             nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) {
        throw std::runtime_error("Could not create DVD output window");
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    original_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(window_proc)));
    if (!original_proc) {
        DestroyWindow(window);
        window = nullptr;
        throw std::runtime_error("Could not subclass DVD output window");
    }
}

Output::State::~State() {
    reset_audio();
    swr_free(&resampler);
    sws_freeContext(scaler);
    if (window) {
        DestroyWindow(window);
    }
}

LRESULT CALLBACK Output::State::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(hwnd, &paint);
        if (dc && state && !state->shared_renderer) {
            state->paint(state->canvas->dc);
            BitBlt(dc, 0, 0, state->canvas->size.cx, state->canvas->size.cy, state->canvas->dc, 0,
                   0, SRCCOPY);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    return state ? CallWindowProcW(state->original_proc, hwnd, message, wparam, lparam)
                 : DefWindowProcW(hwnd, message, wparam, lparam);
}

void Output::State::layout() {
    // cnc-ddraw reports logical 640x480 through GetClientRect on the game window.
    SIZE size{};
    if (shared_renderer) {
        const auto dimensions = SendMessageW(parent, frame_message, 0, 0);
        size = {LOWORD(dimensions), HIWORD(dimensions)};
        if (size.cx <= 0 || size.cy <= 0 || size.cx > 4096 || size.cy > 4096) {
            throw std::runtime_error("DVD display surface is unavailable");
        }
    } else {
        size = physical_client_size(parent);
    }
    if (canvas->size.cx != std::max(1L, size.cx) || canvas->size.cy != std::max(1L, size.cy)) {
        canvas = std::make_unique<Canvas>(std::max(1L, size.cx), std::max(1L, size.cy));
    }
    if (shared_renderer) {
        return;
    }
    RECT window_bounds{};
    if (!GetClientRect(window, &window_bounds)) {
        throw std::runtime_error("Could not measure DVD output window");
    }
    if (size.cx != window_bounds.right || size.cy != window_bounds.bottom) {
        if (!SetWindowPos(window, nullptr, 0, 0, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE)) {
            throw std::runtime_error("Could not size DVD output window");
        }
    }
}

void Output::State::present() {
    if (shared_renderer) {
        if (visible) {
            paint(canvas->dc);
            if (!GdiFlush() ||
                !SendMessageW(parent, frame_message, reinterpret_cast<WPARAM>(canvas->dc),
                              MAKELPARAM(canvas->size.cx, canvas->size.cy))) {
                throw std::runtime_error("Could not present DVD frame");
            }
        }
    } else {
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
    }
}

void Output::State::paint(HDC dc) {
    const RECT bounds{0, 0, canvas->size.cx, canvas->size.cy};
    FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    if (pixels.empty() || bounds.right <= 0 || bounds.bottom <= 0) {
        return;
    }
    const int source_width = source.right - source.left;
    const int source_height = source.bottom - source.top;
    const int dest_width = dest.right - dest.left;
    const int dest_height = dest.bottom - dest.top;
    if (source.left < 0 || source.top < 0 || source.right > width || source.bottom > height ||
        source_width <= 0 || source_height <= 0 || dest_width <= 0 || dest_height <= 0) {
        return;
    }
    const int logical_width = client.right - client.left;
    const int logical_height = client.bottom - client.top;
    int viewport_width = bounds.right;
    int viewport_height = bounds.bottom;
    if (static_cast<std::int64_t>(viewport_width) * logical_height >
        static_cast<std::int64_t>(viewport_height) * logical_width) {
        viewport_width = static_cast<int>(static_cast<std::int64_t>(viewport_height) *
                                          logical_width / logical_height);
    } else {
        viewport_height = static_cast<int>(static_cast<std::int64_t>(viewport_width) *
                                           logical_height / logical_width);
    }
    const int viewport_left = (bounds.right - viewport_width) / 2;
    const int viewport_top = (bounds.bottom - viewport_height) / 2;
    const auto map_x = [&](int x) {
        return viewport_left + static_cast<int>(static_cast<std::int64_t>(x - client.left) *
                                                viewport_width / logical_width);
    };
    const auto map_y = [&](int y) {
        return viewport_top + static_cast<int>(static_cast<std::int64_t>(y - client.top) *
                                               viewport_height / logical_height);
    };
    const int left = map_x(dest.left);
    const int top = map_y(dest.top);
    const int right = map_x(dest.right);
    const int bottom = map_y(dest.bottom);
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = width;
    bitmap.bmiHeader.biHeight = -height;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc, left, top, right - left, bottom - top, source.left, source.top, source_width,
                  source_height, pixels.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
    if (!caption.empty()) {
        // This mapped picture keeps its baked-in letterbox and native destination rectangle.
        const bool below = caption_style.placement == CaptionPlacement::Below;
        const auto caption_x = [&](int x) { return left + MulDiv(right - left, x, 640); };
        const auto caption_y = [&](int y) { return top + MulDiv(bottom - top, y, 480); };
        const RECT area{caption_x(20), caption_y(below ? 360 : 120), caption_x(620),
                        caption_y(below ? 420 : 360)};
        playback::paint_caption(dc, area, caption, caption_style, caption_x(620) - caption_x(20));
    }
}

void Output::State::ensure_audio() {
    if (voice) {
        return;
    }
    if (!engine) {
        const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
            check(apartment, "Audio apartment");
        }
        audio_apartment = SUCCEEDED(apartment);
        check(XAudio2Create(&engine), "XAudio2 engine");
        check(engine->CreateMasteringVoice(&master), "XAudio2 mastering voice");
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 48000 * format.nBlockAlign;
    callback.error.store(S_OK);
    check(engine->CreateSourceVoice(&voice, &format, 0, 4.0f, &callback), "XAudio2 source voice");
    check(voice->SetFrequencyRatio(static_cast<float>(speed)), "DVD audio speed");
    if (!paused) {
        check(voice->Start(), "Start DVD audio");
    }
}

void Output::State::reclaim() {
    if (!voice) {
        return;
    }
    check(callback.error.load(std::memory_order_acquire), "DVD audio voice");
    const auto completed = callback.completed.load(std::memory_order_acquire);
    while (reclaimed < completed) {
        queued_bytes -= buffers.front()->size();
        buffers.pop_front();
        ++reclaimed;
    }
}

void Output::State::reset_audio() noexcept {
    if (voice) {
        voice->Stop();
        voice->FlushSourceBuffers();
        voice->DestroyVoice();
        voice = nullptr;
    }
    buffers.clear();
    queued_bytes = 0;
    callback.completed.store(0);
    reclaimed = 0;
    callback.error.store(S_OK);
    if (master) {
        master->DestroyVoice();
        master = nullptr;
    }
    engine.Reset();
    if (audio_apartment) {
        CoUninitialize();
        audio_apartment = false;
    }
}

Output::Output(HWND parent) : state_(std::make_unique<State>(parent)) {}

Output::~Output() = default;

void Output::rectangles(RECT source, RECT dest, RECT client) {
    auto valid = [](RECT rect) {
        const auto width = static_cast<std::int64_t>(rect.right) - rect.left;
        const auto height = static_cast<std::int64_t>(rect.bottom) - rect.top;
        return rect.left >= -4096 && rect.left <= 4096 && rect.top >= -4096 && rect.top <= 4096 &&
               rect.right >= -4096 && rect.right <= 4096 && rect.bottom >= -4096 &&
               rect.bottom <= 4096 && width > 0 && width <= 4096 && height > 0 && height <= 4096;
    };
    if (!valid(source) || !valid(dest) || !valid(client) || source.left < 0 || source.top < 0 ||
        source.right > max_width || source.bottom > max_height) {
        throw std::runtime_error("Invalid DVD output rectangles");
    }
    state_->source = source;
    state_->dest = dest;
    state_->client = client;
    state_->layout();
    state_->present();
}

void Output::video(const AVFrame& frame) {
    state_->layout();
    if (frame.width <= 0 || frame.width > max_width || frame.height <= 0 ||
        frame.height > max_height || frame.format < 0 || frame.width < state_->source.right ||
        frame.height < state_->source.bottom) {
        throw std::runtime_error("Unsupported DVD video frame");
    }
    state_->scaler = sws_getCachedContext(
        state_->scaler, frame.width, frame.height, static_cast<AVPixelFormat>(frame.format),
        frame.width, frame.height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!state_->scaler) {
        throw std::runtime_error("Could not create DVD colour converter");
    }
    auto* scaler = state_->scaler;
    const auto* input_coefficients = sws_getCoefficients(colour_space(frame.colorspace));
    const auto* output_coefficients = sws_getCoefficients(SWS_CS_ITU601);
    check(sws_setColorspaceDetails(scaler, input_coefficients,
                                   frame.color_range == AVCOL_RANGE_JPEG ? 1 : 0,
                                   output_coefficients, 1, 0, 1 << 16, 1 << 16),
          "DVD colour range");
    const auto pitch = frame.width * 4;
    state_->pixels.resize(static_cast<std::size_t>(pitch) * frame.height);
    std::uint8_t* planes[]{state_->pixels.data(), nullptr, nullptr, nullptr};
    const int strides[]{pitch, 0, 0, 0};
    const auto lines =
        sws_scale(scaler, frame.data, frame.linesize, 0, frame.height, planes, strides);
    if (lines != frame.height) {
        throw std::runtime_error("Could not convert DVD video frame");
    }
    state_->width = frame.width;
    state_->height = frame.height;
    state_->present();
}

void Output::audio(const AVFrame& frame, int first_sample, int sample_count) {
    if (frame.sample_rate != 48000 || frame.ch_layout.nb_channels != 2 ||
        (frame.format != AV_SAMPLE_FMT_S16 && frame.format != AV_SAMPLE_FMT_S32) ||
        first_sample < 0 || sample_count < 0 || first_sample > frame.nb_samples ||
        sample_count > frame.nb_samples - first_sample || !frame.extended_data ||
        !frame.extended_data[0]) {
        throw std::runtime_error("Unsupported DVD audio frame");
    }
    if (!sample_count) {
        return;
    }
    const auto bytes = static_cast<std::size_t>(sample_count) * 4;
    state_->reclaim();
    if (bytes > max_audio_bytes || state_->buffers.size() >= max_audio_buffers ||
        state_->queued_bytes > max_audio_bytes - bytes) {
        throw std::runtime_error("DVD audio queue is full");
    }
    auto pcm = std::make_unique<std::vector<std::uint8_t>>(bytes);
    if (frame.format == AV_SAMPLE_FMT_S16) {
        std::memcpy(pcm->data(),
                    frame.extended_data[0] + static_cast<std::size_t>(first_sample) * 4, bytes);
    } else {
        if (!state_->resampler) {
            check(swr_alloc_set_opts2(&state_->resampler, &frame.ch_layout, AV_SAMPLE_FMT_S16,
                                      48000, &frame.ch_layout, AV_SAMPLE_FMT_S32, 48000, 0,
                                      nullptr),
                  "DVD audio converter");
            check(swr_init(state_->resampler), "Initialize DVD audio converter");
        }
        auto* output = pcm->data();
        const auto* input = frame.extended_data[0] + static_cast<std::size_t>(first_sample) * 8;
        const auto converted =
            swr_convert(state_->resampler, &output, sample_count, &input, sample_count);
        check(converted, "Convert DVD audio");
        if (converted != sample_count) {
            throw std::runtime_error("DVD audio converter delayed samples");
        }
    }
    state_->ensure_audio();
    state_->buffers.push_back(std::move(pcm));
    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(bytes);
    buffer.pAudioData = state_->buffers.back()->data();
    const auto result = state_->voice->SubmitSourceBuffer(&buffer);
    if (FAILED(result)) {
        state_->buffers.pop_back();
        check(result, "Submit DVD audio");
    }
    state_->queued_bytes += bytes;
}

void Output::caption(std::wstring text, const CaptionStyle& style) {
    if (state_->caption == text && state_->caption_style == style) {
        return;
    }
    state_->caption = std::move(text);
    state_->caption_style = style;
    state_->layout();
    state_->present();
}

bool Output::drained() {
    state_->reclaim();
    return state_->buffers.empty();
}

std::int64_t Output::played() const {
    if (!state_->voice) {
        return 0;
    }
    XAUDIO2_VOICE_STATE status{};
    state_->voice->GetState(&status);
    const auto samples = status.SamplesPlayed;
    return static_cast<std::int64_t>((samples / 8) * 15 + (samples % 8) * 15 / 8);
}

void Output::pause(bool paused) {
    if (state_->paused == paused) {
        return;
    }
    if (state_->voice) {
        check(paused ? state_->voice->Stop() : state_->voice->Start(),
              paused ? "Pause DVD audio" : "Resume DVD audio");
    }
    state_->paused = paused;
}

void Output::speed(unsigned multiplier) {
    if (state_->voice) {
        check(state_->voice->SetFrequencyRatio(static_cast<float>(multiplier)), "DVD audio speed");
    }
    state_->speed = multiplier;
}

void Output::clear() {
    state_->reset_audio();
    state_->pixels.clear();
    state_->width = 0;
    state_->height = 0;
    state_->caption.clear();
    state_->present();
}

void Output::discard_audio() {
    state_->reset_audio();
}

void Output::show(bool visible) {
    state_->layout();
    state_->visible = visible;
    // The wrapper owns presentation. A visible child would be repainted from the old surface.
    ShowWindow(state_->window, visible && !state_->shared_renderer ? SW_SHOWNA : SW_HIDE);
    state_->present();
}

HWND Output::window() const {
    return state_->window;
}

}
