#include "movie.h"

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
};

thread_local std::unordered_map<Callback*, std::unique_ptr<Callback>> callbacks;

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
    trace_movie("callback", movie(callback->movie), time);
    return Error::None;
}

}

void run_callbacks(MovieHandle handle, std::int32_t before, std::int32_t after) {
    const auto scale = movie(handle).media->timescale;
    std::vector<Callback*> ready;
    for (const auto& [key, value] : callbacks) {
        if (value->movie != handle || !value->procedure) {
            continue;
        }
        const auto target = static_cast<std::int64_t>(value->time) * scale;
        const auto previous = static_cast<std::int64_t>(before) * value->scale;
        const auto current = static_cast<std::int64_t>(after) * value->scale;
        if (value->direction != Direction::Backward && current > previous && previous <= target &&
            current >= target) {
            ready.push_back(key);
        }
    }
    for (const auto key : ready) {
        const auto found = callbacks.find(key);
        if (found == callbacks.end() || !found->second->procedure) {
            continue;
        }
        const auto procedure = found->second->procedure;
        const auto context = found->second->context;
        found->second->procedure = nullptr;
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
