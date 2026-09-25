#include "movie.h"

namespace {

using namespace playback;

std::int32_t __cdecl count(MovieHandle handle) {
    return static_cast<std::int32_t>(movie(handle).tracks.size());
}

TrackHandle __cdecl by_index(MovieHandle handle, std::int32_t index) {
    const auto& tracks = movie(handle).tracks;
    if (index <= 0 || static_cast<std::size_t>(index) > tracks.size()) {
        return nullptr;
    }
    return &tracks[index - 1]->pointer;
}

TrackHandle __cdecl get_media(TrackHandle handle) {
    track(handle);
    return handle;
}

std::int32_t __cdecl duration(TrackHandle handle) {
    const auto value = track(handle).media->duration;
    if (value > INT32_MAX) {
        unsupported(Selector::GetTrackDuration, "Track duration exceeds the legacy time range", 0);
    }
    return static_cast<std::int32_t>(value);
}

std::int32_t __cdecl offset(TrackHandle handle) {
    std::uint64_t value = 0;
    for (const auto& edit : track(handle).media->edits) {
        if (edit.media_time != -1) {
            break;
        }
        if (edit.duration > INT32_MAX - value) {
            unsupported(Selector::GetTrackOffset, "Track offset exceeds the legacy time range", 0);
        }
        value += edit.duration;
    }
    return static_cast<std::int32_t>(value);
}

std::uint8_t __cdecl get_enabled(TrackHandle handle) {
    return track(handle).enabled;
}

Error __cdecl load(TrackHandle handle, std::int32_t start, std::int32_t duration, std::uint32_t) {
    const auto& value = *track(handle).media;
    // Movie files are already retained in memory for their entire lifetime.
    return start < 0 || duration < 0 ||
                   static_cast<std::uint64_t>(start) + duration > value.duration
               ? Error::Parameter
               : Error::None;
}

void __cdecl set_enabled(TrackHandle handle, std::uint8_t enabled) {
    auto& value = track(handle);
    const bool active = enabled != 0;
    if (value.enabled == active) {
        return;
    }
    auto& owner = movie(value.owner);
    refresh_time(owner);
    value.enabled = active;
    owner.redraw = true;
    value.displayed.reset();
    if (value.media->handler == "soun") {
        owner.audio.reset();
        sync_audio(owner);
    }
}

void __cdecl handler_description(TrackHandle handle, std::uint32_t* type, std::uint8_t* name,
                                 std::uint32_t* manufacturer) {
    const auto& handler = track(handle).media->handler;
    if (type) {
        *type = 0;
        for (unsigned char letter : handler) {
            *type = (*type << 8) | letter;
        }
    }
    if (name) {
        *name = 0;
    }
    if (manufacturer) {
        *manufacturer = 0;
    }
}

void __cdecl set_hints(TrackHandle handle, std::uint32_t flags, std::uint32_t mask) {
    constexpr auto loop = 1u << 1;
    constexpr auto high_quality = 1u << 8;
    constexpr auto offscreen = 1u << 12;
    constexpr auto avoid_overlay = 1u << 16;
    // Loop is a buffering hint; the time base or caller controls actual repetition.
    if (flags & mask & ~(loop | high_quality | offscreen | avoid_overlay)) {
        trace_value("media_play_flags", flags);
        trace_value("media_play_mask", mask);
        unsupported(Selector::SetMediaPlayHints, "Unsupported media playback hint", 0);
    }
    auto& value = track(handle);
    value.hints = (value.hints & ~mask) | (flags & mask);
}

}

Entry track_entry(Selector selector) {
    static const EntryBinding entries[] = {
        bind_entry(Selector::GetMovieTrackCount, count),
        bind_entry(Selector::GetMovieIndTrack, by_index),
        bind_entry(Selector::GetTrackMedia, get_media),
        bind_entry(Selector::GetMediaHandler, get_media),
        bind_entry(Selector::GetTrackDuration, duration),
        bind_entry(Selector::GetTrackOffset, offset),
        bind_entry(Selector::GetTrackEnabled, get_enabled),
        bind_entry(Selector::SetTrackEnabled, set_enabled),
        bind_entry(Selector::LoadTrackIntoRam, load),
        bind_entry(Selector::GetMediaHandlerDescription, handler_description),
        bind_entry(Selector::SetMediaPlayHints, set_hints),
    };
    return find_entry(selector, entries);
}
