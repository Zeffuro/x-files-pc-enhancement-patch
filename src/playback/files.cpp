#include "movie.h"
#include "media/files.h"

#include <windows.h>
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
        movie->filename = file->second.filename().string();
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

std::vector<MovieHandle> pause_movies() {
    std::vector<MovieHandle> result;
    for (const auto& [handle, value] : movies) {
        refresh_time(*value);
        if (value->rate) {
            result.push_back(handle);
            value->rate = 0;
            sync_audio(*value);
        }
    }
    return result;
}

void resume_movies(const std::vector<MovieHandle>& handles) {
    for (const auto handle : handles) {
        if (movie_exists(handle)) {
            auto& value = movie(handle);
            value.rate = unit_rate;
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
