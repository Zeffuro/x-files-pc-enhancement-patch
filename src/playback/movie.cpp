#include "movie.h"

namespace playback {

void trace_movie(const char* event, const Movie& movie, std::int32_t value) {
    static const bool enabled =
        GetEnvironmentVariableW(L"XFILES_PATCH_TRACE_MOVIES", nullptr, 0) != 0;
    if (enabled) {
        trace_value((std::string(event) + " " + movie.filename).c_str(), value);
    }
}

}

namespace {

using namespace playback;

std::uint8_t __cdecl get_active(MovieHandle handle) {
    return movie(handle).active;
}

std::int32_t __cdecl duration(MovieHandle handle) {
    const auto value = movie(handle).media->duration;
    if (value > INT32_MAX) {
        unsupported(Selector::GetMovieDuration, "Movie duration exceeds the legacy time range", 0);
    }
    return static_cast<std::int32_t>(value);
}

std::int32_t __cdecl timescale(MovieHandle handle) {
    return static_cast<std::int32_t>(movie(handle).media->timescale);
}

std::int16_t __cdecl volume(MovieHandle handle) {
    return movie(handle).volume;
}

void __cdecl set_volume(MovieHandle handle, std::int16_t value) {
    auto& state = movie(handle);
    state.volume = value;
    if (state.audio) {
        state.audio->volume(value);
    }
}

void __cdecl set_active(MovieHandle handle, std::uint8_t active) {
    auto& state = movie(handle);
    refresh_time(state);
    if (state.active == (active != 0)) {
        return;
    }
    trace_movie("active", state, active);
    state.active = active != 0;
    sync_audio(state);
}

void __cdecl set_drawing_callback(MovieHandle handle, DrawingMode mode, DrawingCallback callback,
                                  std::int32_t context) {
    if (mode != DrawingMode::WhenChanged && mode != DrawingMode::Always) {
        unsupported(Selector::SetMovieDrawingCompleteProc, "Unknown movie drawing callback mode",
                    0);
    }
    auto& value = movie(handle);
    value.drawing_mode = mode;
    value.drawing_callback = callback;
    value.drawing_context = context;
}

void __cdecl get_box(MovieHandle handle, quickdraw::Rect* bounds) {
    if (bounds) {
        *bounds = movie(handle).box;
    }
}

void __cdecl set_box(MovieHandle handle, const quickdraw::Rect* bounds) {
    if (bounds) {
        movie(handle).box = *bounds;
        movie(handle).redraw = true;
    }
}

void __cdecl get_world(MovieHandle handle, quickdraw::Port** port, void** device) {
    if (port) {
        *port = movie(handle).port;
    }
    if (device) {
        *device = nullptr;
    }
}

void __cdecl set_world(MovieHandle handle, quickdraw::Port* port, void* device) {
    if (device) {
        unsupported(Selector::SetMovieGWorld, "Movie graphics devices are not supported", 0);
    }
    movie(handle).port = port;
    movie(handle).redraw = true;
}

}

Entry movie_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::GetMovieDuration, duration),
        bind_entry(Selector::GetMovieTimeScale, timescale),
        bind_entry(Selector::GetMovieVolume, volume),
        bind_entry(Selector::SetMovieVolume, set_volume),
        bind_entry(Selector::SetMovieActive, set_active),
        bind_entry(Selector::GetMovieActive, get_active),
        bind_entry(Selector::SetMovieDrawingCompleteProc, set_drawing_callback),
        bind_entry(Selector::GetMovieBox, get_box),
        bind_entry(Selector::SetMovieBox, set_box),
        bind_entry(Selector::GetMovieGWorld, get_world),
        bind_entry(Selector::SetMovieGWorld, set_world),
    };
    if (const auto entry = find_entry(selector, entries)) {
        return entry;
    }
    if (const auto entry = timeline_entry(selector)) {
        return entry;
    }
    if (const auto entry = callback_entry(selector)) {
        return entry;
    }
    if (const auto entry = track_entry(selector)) {
        return entry;
    }
    return movie_file_entry(selector);
}
