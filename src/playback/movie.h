#pragma once

#include "runtime.h"
#include "media/movie.h"
#include "quickdraw/types.h"
#include "audio.h"
#include "media/video.h"
#include "settings.h"

#include <memory>
#include <chrono>

namespace playback {

struct Movie;
using MovieHandle = Movie**;
using DrawingCallback = std::int16_t(__cdecl*)(MovieHandle, std::int32_t);
enum class DrawingMode : std::int32_t { WhenChanged = 0, Always = 1 };
constexpr std::int32_t unit_rate = 1 << 16;

struct Track {
    Track* pointer = this;
    MovieHandle owner = nullptr;
    const media::Track* media = nullptr;
    bool enabled = true;
    std::int16_t volume = 256;
    std::int16_t balance = 0;
    std::int16_t layer = 0;
    std::uint32_t hints = 0;
    std::unique_ptr<media::Video> video;
    std::optional<std::size_t> displayed;
};

using TrackHandle = Track**;

enum class Error : std::int16_t {
    None = 0,
    FileNotFound = -43,
    Parameter = -50,
    TooManyFiles = -42,
    Memory = -108,
    InvalidMovie = -2048,
};

struct Movie {
    Movie* pointer = this;
    std::shared_ptr<media::Movie> media;
    std::string filename;
    std::vector<std::unique_ptr<Track>> tracks;
    quickdraw::Port* port = nullptr;
    quickdraw::Rect box{};
    std::int32_t time = 0;
    std::int32_t serviced_time = 0;
    std::int32_t rate = 0;
    std::int32_t preferred_rate = unit_rate;
    std::int32_t selection_start = 0;
    std::int32_t selection_duration = 0;
    std::chrono::steady_clock::time_point started;
    std::unique_ptr<Audio> audio;
    std::int16_t volume = 256;
    bool active = true;
    DrawingCallback drawing_callback = nullptr;
    DrawingMode drawing_mode = DrawingMode::WhenChanged;
    std::int32_t drawing_context = 0;
    bool redraw = true;
    std::wstring caption;
    CaptionStyle caption_style;
    std::optional<quickdraw::Rect> caption_bounds;
};

Movie& movie(MovieHandle handle);
bool movie_exists(MovieHandle handle);
Track& track(TrackHandle handle);
void task_movies();
std::vector<MovieHandle> pause_movies();
void resume_movies(const std::vector<MovieHandle>& handles);
void task_movie(MovieHandle handle);
void refresh_time(Movie& movie);
void trace_movie(const char* event, const Movie& movie, std::int32_t value);
void sync_audio(Movie& movie);
bool draw_movie(Movie& movie);
std::wstring current_caption(const Movie& movie);
bool draw_captions(Movie& movie, std::wstring text, bool video_changed);
void run_callbacks(MovieHandle handle, std::int32_t before, std::int32_t after);
void release_callbacks(MovieHandle handle = nullptr);

}

Entry movie_entry(Selector selector);
Entry movie_file_entry(Selector selector);
Entry timeline_entry(Selector selector);
Entry callback_entry(Selector selector);
Entry track_entry(Selector selector);
void release_movies();
