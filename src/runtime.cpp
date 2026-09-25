#include "runtime.h"
#include "graphics.h"
#include "memory.h"
#include "playback/movie.h"
#include "playback/components.h"
#include "quickdraw/world.h"
#include "quickdraw/regions.h"
#include "quickdraw/image_codec.h"

#include <windows.h>

namespace {

thread_local unsigned initialization_depth = 0;
thread_local unsigned movie_depth = 0;
thread_local long initialization_flags = 0;

short __cdecl initialize(long flags) {
    trace_value("initialization_flags", static_cast<std::uint32_t>(flags));
    if (flags & ~0x57L) {
        return -50;
    }
    if (!initialization_depth) {
        initialization_flags = flags;
    }
    ++initialization_depth;
    return 0;
}

void __cdecl terminate_runtime() {
    if (initialization_depth) {
        --initialization_depth;
    }
    if (!initialization_depth) {
        release_movies();
        quickdraw::release_recording();
        release_worlds();
        release_regions();
        release_handles();
        release_graphics();
    }
}

short __cdecl enter_movies() {
    if (!initialization_depth) {
        return -50;
    }
    ++movie_depth;
    return 0;
}

void __cdecl exit_movies() {
    if (movie_depth) {
        --movie_depth;
    }
}
}

Entry runtime_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::CallComponent, playback::call_component),
        bind_entry(Selector::QTMLInitInternals, initialize),
        bind_entry(Selector::QTMLTermInternals, terminate_runtime),
        bind_entry(Selector::EnterMovies, enter_movies),
        bind_entry(Selector::ExitMovies, exit_movies),
    };
    if (const auto entry = find_entry(selector, entries)) {
        return entry;
    }
    if (const auto entry = graphics_entry(selector)) {
        return entry;
    }
    if (const auto entry = world_entry(selector)) {
        return entry;
    }
    if (const auto entry = region_entry(selector)) {
        return entry;
    }
    if (const auto entry = picture_entry(selector)) {
        return entry;
    }
    if (const auto entry = image_codec_entry(selector)) {
        return entry;
    }
    if (const auto entry = recording_entry(selector)) {
        return entry;
    }
    if (const auto entry = movie_entry(selector)) {
        return entry;
    }
    return memory_entry(selector);
}
