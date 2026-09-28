#include "movie.h"
#include "inspection.h"
#include "media/files.h"
#include "enhancements/rumble.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <unordered_map>

namespace playback {
namespace {

#pragma pack(push, 2)

struct FileSpec {
    std::int16_t volume;
    std::int32_t directory;
    std::uint8_t length;
    char name[255];
};

#pragma pack(pop)

static_assert(offsetof(FileSpec, length) == 6);
static_assert(sizeof(FileSpec) == 262);

thread_local std::unordered_map<short, std::filesystem::path> files;
thread_local std::unordered_map<MovieHandle, std::unique_ptr<Movie>> movies;
thread_local std::uint64_t next_inspection_id = 0;

Error __cdecl open_file(const FileSpec* spec, short* reference, std::int8_t permission) {
    if (!spec || !reference || spec->volume || spec->directory || permission != 1 ||
        !spec->length) {
        return Error::Parameter;
    }
    *reference = 0;
    try {
        const auto path = media::locate_file(std::string(spec->name, spec->length));
        if (path.empty()) {
            return Error::FileNotFound;
        }
        trace_value(path.filename().string().c_str(), 0);
        for (short id = 1; id < 32767; ++id) {
            if (!files.contains(id)) {
                files.emplace(id, path);
                *reference = id;
                return Error::None;
            }
        }
        return Error::TooManyFiles;
    } catch (const std::bad_alloc&) {
        return Error::Memory;
    } catch (const std::filesystem::filesystem_error&) {
        return Error::FileNotFound;
    }
}

Error __cdecl close_file(short reference) {
    return files.erase(reference) ? Error::None : Error::Parameter;
}

Error __cdecl from_file(MovieHandle* output, short reference, short* resource, std::uint8_t* name,
                        short flags, std::uint8_t* changed) {
    if (!output) {
        return Error::Parameter;
    }
    *output = nullptr;
    const auto file = files.find(reference);
    if (file == files.end() || (flags & ~1) || (resource && *resource > 0)) {
        return Error::Parameter;
    }
    try {
        auto movie = std::make_unique<Movie>();
        movie->inspection_id = ++next_inspection_id;
        movie->filename = file->second.filename().string();
        movie->source_path = file->second;
        std::vector<wchar_t> executable(32768);
        const auto length =
            GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length && length < executable.size()) {
            const auto game_root = std::filesystem::path(executable.data()).parent_path();
            const auto relative = file->second.lexically_relative(game_root);
            if (!relative.empty() && *relative.begin() != "..") {
                movie->relative_path = relative;
            }
        }
        if (movie->relative_path.empty()) {
            movie->relative_path = file->second.filename();
            auto parent = file->second.parent_path().filename().wstring();
            std::transform(parent.begin(), parent.end(), parent.begin(), std::towupper);
            if (parent == L"XV" || parent == L"XN" || parent == L"XS" || parent == L"XT" ||
                parent == L"XG") {
                movie->relative_path = parent / movie->relative_path;
            }
        }
        movie->media = std::make_shared<media::Movie>(media::Movie::open(file->second));
        if (movie->media->duration > INT32_MAX || movie->media->timescale > INT32_MAX) {
            return Error::InvalidMovie;
        }
        movie->active = (flags & 1) != 0;
        for (const auto& media_track : movie->media->tracks) {
            auto track = std::make_unique<Track>();
            track->owner = &movie->pointer;
            track->media = &media_track;
            track->enabled = (media_track.flags & 1) != 0;
            movie->tracks.push_back(std::move(track));
        }
        for (const auto& track : movie->media->tracks) {
            if (track.handler == "vide" && !track.descriptions.empty()) {
                movie->box.right = static_cast<std::int16_t>(track.descriptions[0].width);
                movie->box.bottom = static_cast<std::int16_t>(track.descriptions[0].height);
                break;
            }
        }
        const auto handle = &movie->pointer;
        movies.emplace(handle, std::move(movie));
        *output = handle;
        if (resource) {
            *resource = 0;
        }
        if (name) {
            *name = 0;
        }
        if (changed) {
            *changed = 0;
        }
        return Error::None;
    } catch (const std::bad_alloc&) {
        return Error::Memory;
    } catch (const std::exception& error) {
        unsupported(Selector::NewMovieFromFile, error.what(), 0);
    }
}

void __cdecl dispose(MovieHandle handle) {
    if (const auto found = movies.find(handle); found != movies.end()) {
        enhancements::cancel_rumble(reinterpret_cast<std::uintptr_t>(found->second.get()));
    }
    release_callbacks(handle);
    movies.erase(handle);
}

}

Movie& movie(MovieHandle handle) {
    const auto found = movies.find(handle);
    if (found == movies.end()) {
        unsupported(Selector::NewMovieFromFile, "Unknown movie handle", 0);
    }
    return *found->second;
}

bool movie_exists(MovieHandle handle) {
    return movies.contains(handle);
}

std::uint64_t inspect_time(std::uint64_t id) {
    for (const auto& [handle, value] : movies) {
        if (value->inspection_id == id) {
            const auto elapsed = value->rate
                                     ? std::chrono::duration<double>(
                                           std::chrono::steady_clock::now() - value->started)
                                           .count()
                                     : 0.0;
            return std::min(
                value->media->duration,
                static_cast<std::uint64_t>(std::max(0, value->time)) +
                    static_cast<std::uint64_t>(std::max(0.0, elapsed) * value->media->timescale));
        }
    }
    return 0;
}

std::vector<MovieSnapshot> inspect_movies() {
    std::vector<MovieSnapshot> result;
    for (const auto& [handle, value] : movies) {
        bool video = false, audio = false;
        for (const auto& track : value->tracks) {
            video |= track->enabled && track->media->handler == "vide";
            audio |= track->enabled && track->media->handler == "soun";
        }
        result.push_back({value->inspection_id,
                          value->relative_path,
                          value->time,
                          value->media->duration,
                          value->media->timescale,
                          value->active,
                          value->rate != 0,
                          video,
                          audio,
                          value->override_cues.has_value(),
                          value->caption,
                          {},
                          value->last_frame_draw,
                          value->box.left,
                          value->box.top,
                          value->box.right - value->box.left,
                          value->box.bottom - value->box.top});
        auto& preview = result.back().preview;
        for (const auto& track : value->tracks) {
            if (track.get() != value->last_drawn_track || !track->video || !track->displayed) {
                continue;
            }
            const auto& frame = track->video->last_frame();
            if (!frame.width || !frame.height || frame.pixels.empty()) {
                continue;
            }
            result.back().image = media::frame_reference(*track->media, *track->displayed);
            const auto scale = std::min(1.0, std::min(320.0 / frame.width, 180.0 / frame.height));
            preview.width = std::max(1u, static_cast<unsigned>(frame.width * scale));
            preview.height = std::max(1u, static_cast<unsigned>(frame.height * scale));
            preview.pixels.resize(preview.width * preview.height * 4);
            for (unsigned y = 0; y < preview.height; ++y) {
                for (unsigned x = 0; x < preview.width; ++x) {
                    const auto src = (y * frame.height / preview.height * frame.width +
                                      x * frame.width / preview.width) *
                                     4;
                    std::copy_n(frame.pixels.data() + src, 4,
                                preview.pixels.data() + (y * preview.width + x) * 4);
                }
            }
            break;
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.video != b.video) {
            return a.video > b.video;
        }
        if (a.active != b.active) {
            return a.active > b.active;
        }
        return a.id < b.id;
    });
    return result;
}

std::vector<MovieHandle> pause_movies() {
    enhancements::stop_rumble();
    std::vector<MovieHandle> result;
    for (const auto& [handle, value] : movies) {
        refresh_time(*value);
        if (value->rate) {
            result.push_back(handle);
            value->rate = 0;
            value->fast_forward.reset();
            sync_audio(*value);
        }
    }
    return result;
}

void resume_movies(const std::vector<MovieHandle>& handles) {
    for (const auto handle : handles) {
        if (movie_exists(handle)) {
            auto& value = movie(handle);
            value.redraw = true;
            value.rate = unit_rate;
            value.fast_forward.reset();
            value.started = std::chrono::steady_clock::now();
            sync_audio(value);
        }
    }
}

void task_movies() {
    std::vector<MovieHandle> handles;
    for (const auto& [handle, value] : movies) {
        handles.push_back(handle);
    }
    for (const auto handle : handles) {
        if (movies.contains(handle)) {
            task_movie(handle);
        }
    }
}

Track& track(TrackHandle handle) {
    for (const auto& [key, value] : movies) {
        for (const auto& track : value->tracks) {
            if (&track->pointer == handle) {
                return *track;
            }
        }
    }
    unsupported(Selector::GetTrackMedia, "Unknown track or media handle", 0);
}

}

Entry movie_file_entry(Selector selector) {
    using namespace playback;
    static const EntryBinding entries[] = {
        bind_entry(Selector::OpenMovieFile, open_file),
        bind_entry(Selector::CloseMovieFile, close_file),
        bind_entry(Selector::NewMovieFromFile, from_file),
        bind_entry(Selector::DisposeMovie, dispose),
    };
    return find_entry(selector, entries);
}

void release_movies() {
    playback::release_callbacks();
    playback::movies.clear();
    playback::files.clear();
}
