#include "movie.h"
#include "menu_colors.h"
#include "settings.h"

#include <algorithm>
#include <stdexcept>

namespace playback {

void refresh_time(Movie& value) {
    if (!value.rate) {
        return;
    }
    if (settings().skip_menu_animation && is_menu_entrance(value.filename)) {
        // Complete through the normal drawing/callback path, preserving menu initialization.
        value.time = static_cast<std::int32_t>(value.media->duration);
        value.rate = 0;
        if (value.audio) {
            value.audio->stop();
        }
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - value.started).count();
    const auto ticks = static_cast<std::int64_t>(elapsed * value.media->timescale);
    if (ticks <= 0) {
        return;
    }
    const auto position = std::min<std::int64_t>(value.media->duration, value.time + ticks);
    value.time = static_cast<std::int32_t>(position);
    value.started += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(static_cast<double>(ticks) / value.media->timescale));
    if (static_cast<std::uint64_t>(position) == value.media->duration) {
        value.rate = 0;
    }
}

void sync_audio(Movie& value) {
    try {
        if (!value.rate || !value.active) {
            if (value.audio) {
                value.audio->stop();
            }
            return;
        }
        if (!value.audio) {
            const Track* selected = nullptr;
            for (const auto& track : value.tracks) {
                if (track->enabled && track->media->handler == "soun") {
                    if (selected) {
                        throw std::runtime_error("Multiple audio tracks require mixing");
                    }
                    selected = track.get();
                }
            }
            if (!selected) {
                return;
            }
            value.audio = std::make_unique<Audio>(*value.media, *selected->media);
            value.audio->balance(selected->balance);
        }
        value.audio->play(value.time, value.media->timescale, value.volume);
    } catch (const std::exception& error) {
        unsupported(Selector::StartMovie, error.what(), 0);
    }
}

void task_movie(MovieHandle handle) {
    auto& value = movie(handle);
    refresh_time(value);
    if (value.active && value.rate && value.audio) {
        try {
            value.audio->refresh(value.time, value.media->timescale);
        } catch (const std::exception& error) {
            unsupported(Selector::MoviesTask, error.what(), 0);
        }
    }
    const auto drawn = value.active && draw_movie(value);
    const auto before = value.serviced_time;
    value.serviced_time = value.time;
    const auto after = value.time;
    const auto callback = value.drawing_callback;
    const auto context = value.drawing_context;
    const auto notify =
        value.active && callback && (drawn || value.drawing_mode == DrawingMode::Always);
    if (notify) {
        callback(handle, context);
    }
    if (movie_exists(handle)) {
        run_callbacks(handle, before, after);
    }
}

namespace {

#pragma pack(push, 2)

struct TimeRecord {
    std::uint32_t low;
    std::int32_t high;
    std::int32_t scale;
    void* base;
};

#pragma pack(pop)

void __cdecl set_rate(MovieHandle handle, std::int32_t rate) {
    if (rate != 0 && rate != unit_rate) {
        unsupported(Selector::SetMovieRate, "Only normal forward playback is supported", 0);
    }
    auto& value = movie(handle);
    refresh_time(value);
    value.rate = rate;
    trace_movie("rate", value, rate);
    value.started = std::chrono::steady_clock::now();
    sync_audio(value);
}

std::int32_t __cdecl get_rate(MovieHandle handle) {
    auto& value = movie(handle);
    refresh_time(value);
    return value.rate;
}

void __cdecl start(MovieHandle handle) {
    auto& value = movie(handle);
    value.active = true;
    value.redraw = true;
    set_rate(handle, value.preferred_rate);
}

void __cdecl stop(MovieHandle handle) {
    set_rate(handle, 0);
}

std::int32_t __cdecl get_time(MovieHandle handle, TimeRecord* record) {
    auto& value = movie(handle);
    refresh_time(value);
    if (record) {
        *record = {static_cast<std::uint32_t>(value.time), 0,
                   static_cast<std::int32_t>(value.media->timescale), handle};
    }
    return value.time;
}

void __cdecl set_time(MovieHandle handle, std::int32_t time) {
    auto& value = movie(handle);
    value.time = std::clamp(time, 0, static_cast<std::int32_t>(value.media->duration));
    // Forward seeks must still deliver crossed callbacks on the next MoviesTask.
    value.serviced_time = std::min(value.serviced_time, value.time);
    trace_movie("seek", value, value.time);
    value.redraw = true;
    value.started = std::chrono::steady_clock::now();
    sync_audio(value);
}

void __cdecl set_selection(MovieHandle handle, std::int32_t start, std::int32_t duration) {
    auto& value = movie(handle);
    if (start < 0 || duration < 0 ||
        static_cast<std::uint64_t>(start) + duration > value.media->duration) {
        unsupported(Selector::SetMovieSelection, "Movie selection exceeds duration", 0);
    }
    trace_movie("selection_start", value, start);
    trace_movie("selection_duration", value, duration);
    value.selection_start = start;
    value.selection_duration = duration;
}

void __cdecl get_selection(MovieHandle handle, std::int32_t* start, std::int32_t* duration) {
    const auto& value = movie(handle);
    if (start) {
        *start = value.selection_start;
    }
    if (duration) {
        *duration = value.selection_duration;
    }
}

void __cdecl task(MovieHandle handle, std::int32_t) {
    thread_local bool servicing = false;
    if (servicing) {
        return;
    }

    struct Guard {
        bool& active;

        explicit Guard(bool& value) : active(value) {
            active = true;
        }

        ~Guard() {
            active = false;
        }
    } guard(servicing);

    MSG message{};
    const auto deadline = GetTickCount64() + 3;
    for (unsigned count = 0; count < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
         ++count) {
        const auto id = message.message;
        if (id == WM_QUIT || (id >= WM_KEYFIRST && id <= WM_KEYLAST) ||
            (id >= WM_MOUSEFIRST && id <= WM_MOUSELAST) || id == WM_INPUT) {
            break;
        }
        if (!PeekMessageW(&message, nullptr, id, id, PM_REMOVE)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
        if (GetTickCount64() >= deadline) {
            break;
        }
    }
    if (handle) {
        if (movie_exists(handle)) {
            task_movie(handle);
        }
    } else {
        task_movies();
    }
}

Error __cdecl update(MovieHandle handle) {
    movie(handle).redraw = true;
    task_movie(handle);
    return Error::None;
}

Error __cdecl load(MovieHandle handle, std::int32_t start, std::int32_t duration, std::int32_t) {
    const auto& value = movie(handle);
    return start < 0 || duration < 0 ||
                   static_cast<std::uint64_t>(start) + duration > value.media->duration
               ? Error::Parameter
               : Error::None;
}

std::int32_t __cdecl preferred_rate(MovieHandle handle) {
    return movie(handle).preferred_rate;
}

void __cdecl set_preferred_rate(MovieHandle handle, std::int32_t rate) {
    movie(handle).preferred_rate = rate;
}

}
}

Entry timeline_entry(Selector selector) {
    using namespace playback;
    static const EntryBinding entries[] = {
        bind_entry(Selector::StartMovie, start),
        bind_entry(Selector::StopMovie, stop),
        bind_entry(Selector::GetMovieRate, get_rate),
        bind_entry(Selector::SetMovieRate, set_rate),
        bind_entry(Selector::GetMovieTime, get_time),
        bind_entry(Selector::SetMovieTimeValue, set_time),
        bind_entry(Selector::GetMovieSelection, get_selection),
        bind_entry(Selector::SetMovieSelection, set_selection),
        bind_entry(Selector::MoviesTask, task),
        bind_entry(Selector::UpdateMovie, update),
        bind_entry(Selector::LoadMovieIntoRam, load),
        bind_entry(Selector::GetMoviePreferredRate, preferred_rate),
        bind_entry(Selector::SetMoviePreferredRate, set_preferred_rate),
    };
    return find_entry(selector, entries);
}
