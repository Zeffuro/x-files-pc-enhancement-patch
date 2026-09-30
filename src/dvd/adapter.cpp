#include "native.h"
#include "output.h"
#include "player.h"
#include "files.h"
#include "captions.h"
#include "clock.h"
#include "input.h"
#include "focus.h"
#include "media/mpeg_timing.h"
#include "diagnostics/log_file.h"
#include "settings.h"
#include "transcript/dvd_bridge.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>

namespace dvd {
namespace {

class Adapter;
thread_local Adapter* adapters = nullptr;
thread_local unsigned tools_pause_depth = 0;

class Device final : public Sink {
public:
    explicit Device(HWND parent) : output(parent) {}

    void video(const AVFrame& frame) override {
        output.video(frame);
    }

    void audio(const AVFrame& frame, int first, int count) override {
        if (!muted) {
            output.audio(frame, first, count);
        }
    }

    bool drained() override {
        return muted || output.drained();
    }

    void pause(bool value) override {
        output.pause(value);
    }

    void clear() override {
        if (retain_video) {
            output.discard_audio();
        } else {
            output.clear();
        }
    }

    Output output;
    bool muted = false;
    bool retain_video = false;
};

bool selected(const std::filesystem::path& path) {
    auto name = path.filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    return name == L"ddigital1.vob" || name == L"teaser.vob" || Captions::supported(path);
}

class Adapter final : public Native {
public:
    Adapter() {
        timer_window_ = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                        nullptr, nullptr);
        if (!timer_window_) {
            throw std::runtime_error("Cannot create DVD playback timer");
        }
        SetWindowLongPtrW(timer_window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        if (!SetWindowLongPtrW(timer_window_, GWLP_WNDPROC,
                               reinterpret_cast<LONG_PTR>(&messages))) {
            DestroyWindow(timer_window_);
            throw std::runtime_error("Cannot subclass DVD playback timer");
        }
        try {
            acquire_file_hook();
        } catch (...) {
            DestroyWindow(timer_window_);
            throw;
        }
        next_ = adapters;
        adapters = this;
    }

    ~Adapter() override {
        auto** entry = &adapters;
        while (*entry != this) {
            entry = &(*entry)->next_;
        }
        *entry = next_;
        closeMovie(0);
        DestroyWindow(timer_window_);
        release_file_hook();
    }

    int init() override {
        return 1;
    }

    void shutdown() override {
        closeMovie(0);
    }

    void configure() override {}

    void showWindow(int show) override {
        run([&] {
            if (device_) {
                device_->output.show(show != 0);
            }
        });
    }

    void setSourceRect(RECT rect) override {
        source_rect_ = rect;
        rectangles();
    }

    void setDestRect(RECT rect) override {
        dest_rect_ = rect;
        rectangles();
    }

    void setClientRect(RECT rect) override {
        client_rect_ = rect;
        rectangles();
    }

    void closeMovie(int) override {
        tools_resume_ = false;
        cancel_timer();
        if (player_ &&
            (player_->status() == Status::playing || player_->status() == Status::paused)) {
            held_speed_.reset();
        }
        held_speed_.clear();
        if (device_) {
            try {
                device_->output.caption({}, caption_style_);
            } catch (...) {
            }
        }
        player_.reset();
        device_.reset();
        captions_.reset();
        parent_ = nullptr;
        from_ = 0;
        to_ = -1;
    }

    int openMovie(void* parent, char* path) override {
        closeMovie(0);
        error_.fill(0);
        failed_ = false;
        broken_ = false;
        run([&] {
            if (!path || !IsWindow(static_cast<HWND>(parent))) {
                throw std::runtime_error("Invalid DVD movie window or path");
            }
            const std::filesystem::path requested(path);
            clip_ = requested.filename().string();
            const auto options = load_settings();
            speed_key_ = options.movie_speed_key;
            speed_multiplier_ = options.movie_speed;
            speed_mute_ = options.movie_speed_mute;
            gamepad_ = options.gamepad;
            if (!options.dvd_movies || !selected(requested)) {
                throw std::runtime_error("DVD clip uses QuickTime fallback");
            }
            const auto file = movie_path(requested);
            if (file.empty()) {
                throw std::runtime_error("DVD movie is missing");
            }
            if (Captions::supported(requested)) {
                // The native interface has no equivalent of QuickTime's SetTrackEnabled.
                if (options.captions == CaptionMode::Game) {
                    throw std::runtime_error("DVD game-caption mode uses QuickTime fallback");
                }
                captions_ = Captions::load(file);
                caption_enabled_ = options.captions == CaptionMode::On;
                caption_style_ = options.caption_style;
            }
            parent_ = static_cast<HWND>(parent);
            focus_.attach(parent_);
            device_ = std::make_unique<Device>(parent_);
            player_ = std::make_unique<Player>(file, *device_, options.dvd_deinterlace);
            source_rect_ = {0, 0, 704, 480};
            dest_rect_ = client_rect_ = {0, 0, 640, 480};
            device_->output.rectangles(source_rect_, dest_rect_, client_rect_);
            record("open");
        });
        if (failed_) {
            closeMovie(0);
            return 0;
        }
        return 1;
    }

    void playMovie(void*) override {
        run([&] {
            if (!player_ || failed_) {
                throw std::runtime_error("No DVD movie is open");
            }
            broken_ = false;
            if (player_->status() == Status::paused) {
                if (!tools_pause_depth) {
                    player_->resume();
                }
            } else {
                clock_.reset(ticks(from_), GetTickCount64());
                tail_ = false;
                held_speed_.clear();
                device_->muted = false;
                device_->output.speed(speed_ = 1);
                player_->start(clock_.position(), to_ < 0 ? INT64_MAX : ticks(to_),
                               tools_pause_depth != 0);
            }
            device_->output.show(true);
            refresh_captions();
            tools_resume_ = tools_pause_depth != 0;
            if (tools_resume_) {
                cancel_timer();
            } else {
                start_timer();
            }
            record("play");
        });
    }

    void pauseMovie() override {
        run([&] {
            tools_resume_ = false;
            cancel_timer();
            if (player_) {
                player_->pause();
                reset_speed();
            }
        });
    }

    void stopMovie() override {
        run([&] {
            tools_resume_ = false;
            cancel_timer();
            if (player_) {
                player_->stop();
            }
            held_speed_.reset();
            broken_ = true;
            record("skip");
        });
    }

    void seekMovie(int selector, int frame) override {
        run([&] {
            if (!player_ || (selector != 11 && selector != 12 && selector != 13)) {
                throw std::runtime_error("Unsupported DVD seek selector");
            }
            tools_resume_ = false;
            cancel_timer();
            if (player_->status() == Status::stopped) {
                held_speed_.clear();
            } else {
                held_speed_.reset();
            }
            device_->muted = false;
            device_->output.speed(speed_ = 1);
            from_ = selector == 11 ? 0 : frame;
            tail_ = false;
            if (selector == 12) {
                clock_.reset(player_->seek_end(), GetTickCount64());
                update_caption();
                return;
            }
            clock_.reset(ticks(from_), GetTickCount64());
            player_->start(clock_.position(), to_ < 0 ? INT64_MAX : ticks(to_), true);
            update_caption();
        });
    }

    void stepMovie(int frames) override {
        run([&] {
            if (!player_) {
                throw std::runtime_error("No DVD movie is open");
            }
            const auto rate = av_inv_q(player_->info().frame_rate);
            const auto frame = av_rescale_q(player_->position(), media::mpeg::clock, rate) + frames;
            if (frame < 0 || frame > INT_MAX) {
                throw std::runtime_error("Invalid DVD frame");
            }
            seekMovie(13, static_cast<int>(frame));
        });
    }

    int usesWaveDevice() override {
        return 1;
    }

    int usesOverlay() override {
        return 0;
    }

    void handleNotify(void*, unsigned) override {}

    int handleMessages(void*, unsigned message, unsigned wparam, long) override {
        if (message == WM_LBUTTONUP || (message == WM_KEYDOWN && wparam == VK_ESCAPE)) {
            stopMovie();
            return 1;
        }
        return 0;
    }

    long getDriverID() override {
        return 0;
    }

    int isPlaying() override {
        return !failed_ && player_ &&
               (player_->status() == Status::playing ||
                (tools_resume_ && player_->status() == Status::paused));
    }

    int userBreak() override {
        return broken_ ? 1 : 0;
    }

    void* getMovieWnd() override {
        return device_ ? device_->output.window() : nullptr;
    }

    void setPlayFrom(long frame) override {
        from_ = frame;
    }

    void setPlayTo(long frame) override {
        to_ = frame;
    }

    char* getErrorString() override {
        return error_.data();
    }

    static void tools_pause(bool paused) noexcept {
        if (paused) {
            if (tools_pause_depth++ != 0) {
                return;
            }
        } else if (!tools_pause_depth || --tools_pause_depth != 0) {
            return;
        }
        for (auto* adapter = adapters; adapter; adapter = adapter->next_) {
            adapter->set_tools_paused(paused);
        }
    }

private:
    void set_tools_paused(bool paused) noexcept {
        run([&] {
            if (paused && player_ && player_->status() == Status::playing) {
                tools_resume_ = true;
                cancel_timer();
                player_->pause();
                reset_speed();
                record("tools-pause");
            } else if (!paused && tools_resume_) {
                tools_resume_ = false;
                if (!failed_ && player_ && player_->status() == Status::paused) {
                    player_->resume();
                    refresh_captions();
                    start_timer();
                    record("tools-resume");
                }
            }
        });
    }

    template <typename Action> void run(Action action) noexcept {
        try {
            action();
        } catch (const std::exception& error) {
            fail(error.what());
        } catch (...) {
            fail("DVD playback failed");
        }
    }

    void fail(const char* message) noexcept {
        failed_ = true;
        tools_resume_ = false;
        strncpy_s(error_.data(), error_.size(), message, _TRUNCATE);
        record(message);
        cancel_timer();
        try {
            if (player_) {
                held_speed_.reset();
                player_->stop();
            }
        } catch (...) {
        }
    }

    void rectangles() {
        run([&] {
            if (device_) {
                device_->output.rectangles(source_rect_, dest_rect_, client_rect_);
            }
        });
    }

    void refresh_captions() {
        const auto options = load_settings();
        speed_key_ = options.movie_speed_key;
        speed_multiplier_ = options.movie_speed;
        speed_mute_ = options.movie_speed_mute;
        gamepad_ = options.gamepad;
        if (captions_) {
            if (options.captions != CaptionMode::Game) {
                caption_enabled_ = options.captions == CaptionMode::On;
            }
            caption_style_ = options.caption_style;
            update_caption();
        }
    }

    void set_speed(bool fast) {
        const auto speed = fast ? speed_multiplier_ : 1;
        const bool mute = fast && speed_mute_;
        if (speed_ == speed && device_->muted == mute) {
            return;
        }
        const auto position = player_->position();
        const auto limit = to_ < 0 ? INT64_MAX : ticks(to_);
        if (position >= limit) {
            return;
        }
        const bool paused = player_->status() == Status::paused;
        speed_ = speed;
        device_->muted = mute;
        device_->output.speed(speed);

        // Drop queued PCM and rebase its sample counter at the current media time.
        struct RetainVideo {
            bool& enabled;

            explicit RetainVideo(bool& value) : enabled(value) {
                enabled = true;
            }

            ~RetainVideo() {
                enabled = false;
            }
        } retain(device_->retain_video);

        player_->start(position, limit, paused);
        clock_.reset(position, GetTickCount64());
        tail_ = false;
        update_caption();
        record(fast ? ("speed-" + std::to_string(speed) + "x").c_str() : "speed-normal");
    }

    void reset_speed() {
        held_speed_.reset();
        if (player_->status() == Status::playing || player_->status() == Status::paused) {
            set_speed(false);
        } else {
            device_->muted = false;
            device_->output.speed(speed_ = 1);
        }
    }

    void update_caption() {
        if (captions_ && device_ && player_) {
            const auto status = player_->status();
            if (status == Status::playing) {
                transcript::observe_dvd_caption(
                    std::filesystem::path(L"XV") /
                        (std::filesystem::path(clip_).stem().wstring() + L".XMV"),
                    captions_->cues(), captions_->source_cues(), captions_->source_scale(),
                    player_->position());
            }
            const auto visible =
                caption_enabled_ && (status == Status::playing || status == Status::paused);
            device_->output.caption(visible ? captions_->at(player_->position()) : std::wstring{},
                                    caption_style_);
        }
    }

    std::int64_t ticks(long frame) const {
        if (frame < 0) {
            throw std::runtime_error("Invalid DVD frame number");
        }
        return av_rescale_q(frame, av_inv_q(player_->info().frame_rate), media::mpeg::clock);
    }

    void cancel_timer() noexcept {
        if (timer_) {
            KillTimer(timer_window_, timer_);
            timer_ = 0;
        }
    }

    void start_timer() {
        cancel_timer();
        clock_.anchor(GetTickCount64());
        if (!++generation_) {
            ++generation_;
        }
        timer_ = SetTimer(timer_window_, generation_, 10, nullptr);
        if (!timer_) {
            throw std::runtime_error("Cannot start DVD playback timer");
        }
    }

    void tick() {
        run([&] {
            if (!IsWindow(parent_)) {
                throw std::runtime_error("DVD parent window closed");
            }
            held_speed_.context();
            if (speed_ != 1 && !held_speed_.active()) {
                set_speed(false);
            }
            const auto time = clock_.advance(GetTickCount64(), device_->output.played(), speed_,
                                             device_->muted, tail_);
            player_->pump(time);
            update_caption();
            tail_ = player_->audio_finished() && device_->drained();
            if (!device_->muted && !player_->audio_finished() && device_->drained()) {
                throw std::runtime_error("DVD audio cannot advance within the playback buffer");
            }
            if (player_->status() != Status::playing) {
                record("complete");
                cancel_timer();
                held_speed_.clear();
            } else {
                set_speed(poll_speed(held_speed_, parent_, speed_key_, gamepad_));
            }
        });
    }

    void record(const char* event) noexcept {
        try {
            wchar_t path[1024]{};
            const auto size = GetEnvironmentVariableW(L"XFILES_PATCH_LOG", path, _countof(path));
            if (size && size < _countof(path)) {
                diagnostics::append_log(path, "DVD " + clip_ + " " + event + " time=" +
                                                  std::to_string(clock_.position()) + "\r\n");
            }
        } catch (...) {
        }
    }

    static LRESULT CALLBACK messages(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Adapter*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_TIMER && self && self->timer_ && wparam == self->timer_) {
            self->tick();
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    Adapter* next_ = nullptr;
    HWND timer_window_ = nullptr;
    HWND parent_ = nullptr;
    UINT_PTR timer_ = 0;
    UINT_PTR generation_ = 0;
    std::unique_ptr<Device> device_;
    std::unique_ptr<Player> player_;
    std::optional<Captions> captions_;
    CaptionStyle caption_style_;
    bool caption_enabled_ = false;
    RECT source_rect_{};
    RECT dest_rect_{};
    RECT client_rect_{};
    long from_ = 0;
    long to_ = -1;
    Clock clock_;
    playback::HeldFastForward held_speed_{speed_input()};
    FocusBarrier focus_{held_speed_};
    unsigned speed_key_ = 0;
    unsigned speed_multiplier_ = 2;
    unsigned speed_ = 1;
    bool speed_mute_ = true;
    bool gamepad_ = true;
    std::array<char, 256> error_{};
    bool broken_ = false;
    bool failed_ = false;
    bool tail_ = false;
    bool tools_resume_ = false;
    std::string clip_;
};
}
}

extern "C" dvd::Native* __cdecl DLGetInterface() noexcept {
    try {
        return new dvd::Adapter();
    } catch (...) {
        return nullptr;
    }
}

extern "C" void __cdecl XFilesSetToolsPaused(int paused) noexcept {
    dvd::Adapter::tools_pause(paused != 0);
}
