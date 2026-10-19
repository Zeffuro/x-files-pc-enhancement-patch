#include "movie.h"

#include <algorithm>
#include <unordered_map>

namespace playback {
namespace {

enum class CallbackType : std::int16_t { AtTime = 1 };
enum class Direction : std::int32_t { Forward = 1, Backward = 2, Either = 3 };
struct Callback;
using CallbackProc = void(__cdecl*)(Callback*, std::int32_t);

struct Callback {
    MovieHandle movie;
    CallbackProc procedure = nullptr;
    std::int32_t context = 0;
    Direction direction = Direction::Forward;
    std::int32_t time = 0;
    std::int32_t scale = 1;
    std::uint64_t generation = 0;
    bool reached = false;
};

thread_local std::unordered_map<Callback*, std::unique_ptr<Callback>> callbacks;
thread_local std::uint64_t next_generation = 0;

MovieHandle __cdecl timebase(MovieHandle handle) {
    movie(handle);
    return handle;
}

Callback* __cdecl create(MovieHandle base, CallbackType type) {
    movie(base);
    if (type != CallbackType::AtTime) {
        unsupported(Selector::NewCallBack, "Unsupported movie callback type", 0);
    }
    auto callback = std::make_unique<Callback>();
    callback->movie = base;
    const auto result = callback.get();
    callbacks.emplace(result, std::move(callback));
    return result;
}

void __cdecl dispose(Callback* callback) {
    callbacks.erase(callback);
}

void __cdecl cancel(Callback* callback) {
    if (callbacks.contains(callback)) {
        callback->procedure = nullptr;
    }
}

Error __cdecl schedule(Callback* callback, CallbackProc procedure, std::int32_t context,
                       Direction direction, std::int32_t time, std::int32_t scale) {
    if (!callbacks.contains(callback) || !procedure || scale <= 0 || time < 0 ||
        (direction != Direction::Forward && direction != Direction::Backward &&
         direction != Direction::Either)) {
        return Error::Parameter;
    }
    callback->procedure = procedure;
    callback->context = context;
    callback->direction = direction;
    callback->time = time;
    callback->scale = scale;
    callback->generation = ++next_generation;
    callback->reached = false;
    trace_movie("callback", movie(callback->movie), time);
    trace_movie("callback_now", movie(callback->movie), movie(callback->movie).time);
    trace_movie("callback_serviced", movie(callback->movie), movie(callback->movie).serviced_time);
    return Error::None;
}

}

std::int32_t limit_callback_time(Movie& value, std::int32_t proposed, bool forward_tick) {
    auto limited = std::int64_t(proposed);
    for (const auto& [key, callback] : callbacks) {
        if (callback->movie == &value.pointer && callback->procedure &&
            callback->direction != Direction::Backward) {
            const auto deadline =
                (std::int64_t(callback->time) * value.media->timescale + callback->scale - 1) /
                callback->scale;
            limited = std::min(limited, std::max(std::int64_t(value.time), deadline));
        }
    }
    if (forward_tick) {
        for (const auto& [key, callback] : callbacks) {
            if (callback->movie == &value.pointer && callback->procedure &&
                callback->direction != Direction::Backward &&
                limited * callback->scale >=
                    std::int64_t(callback->time) * value.media->timescale) {
                callback->reached = true;
            }
        }
    }
    return static_cast<std::int32_t>(limited);
}

void reset_callback_clock(MovieHandle handle) {
    for (const auto& [key, callback] : callbacks) {
        if (callback->movie == handle) {
            callback->reached = false;
        }
    }
}

void run_callbacks(MovieHandle handle, std::int32_t before, std::int32_t after) {
    const auto scale = movie(handle).media->timescale;
    std::vector<std::pair<Callback*, std::uint64_t>> ready;
    for (const auto& [key, value] : callbacks) {
        if (value->movie != handle || !value->procedure) {
            continue;
        }
        const auto target = static_cast<std::int64_t>(value->time) * scale;
        const auto previous = static_cast<std::int64_t>(before) * value->scale;
        const auto current = static_cast<std::int64_t>(after) * value->scale;
        if (value->direction != Direction::Backward && current >= target &&
            (current > previous || value->reached)) {
            ready.emplace_back(key, value->generation);
        }
    }
    for (const auto& [key, generation] : ready) {
        const auto found = callbacks.find(key);
        if (found == callbacks.end() || !found->second->procedure ||
            found->second->generation != generation) {
            continue;
        }
        const auto procedure = found->second->procedure;
        const auto context = found->second->context;
        found->second->procedure = nullptr;

        struct Dispatch {
            MovieHandle handle;
            std::uint64_t identity;

            explicit Dispatch(MovieHandle value)
                : handle(value), identity(movie(value).inspection_id) {
                ++movie(handle).callback_dispatch_depth;
            }

            ~Dispatch() {
                if (movie_exists(handle) && movie(handle).inspection_id == identity) {
                    --movie(handle).callback_dispatch_depth;
                }
            }
        } dispatch(handle);

        trace_movie("callback_fire_time", movie(handle), after);
        procedure(key, context);
    }
}

void release_callbacks(MovieHandle handle) {
    std::erase_if(callbacks,
                  [handle](const auto& item) { return !handle || item.second->movie == handle; });
}

}

Entry callback_entry(Selector selector) {
    using namespace playback;
    static const EntryBinding entries[] = {
        bind_entry(Selector::GetMovieTimeBase, timebase),
        bind_entry(Selector::NewCallBack, create),
        bind_entry(Selector::DisposeCallBack, dispose),
        bind_entry(Selector::CancelCallBack, cancel),
        bind_entry(Selector::CallMeWhen, schedule),
    };
    return find_entry(selector, entries);
}
