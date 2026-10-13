#include <windows.h>
#include "playback/movie.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
bool adapter_loaded = false, query_present = true, dvd_playing = false;
unsigned borrowed = 0, released = 0, queried = 0, advanced = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int __cdecl test_query() noexcept {
    ++queried;
    return dvd_playing ? 1 : 0;
}

BOOL WINAPI test_module(DWORD flags, LPCWSTR name, HMODULE* module) {
    require(flags == 0 && !wcscmp(name, L"XFilesMpeg.dll"), "Unexpected adapter lookup");
    if (!adapter_loaded) {
        return FALSE;
    }
    ++borrowed;
    *module = reinterpret_cast<HMODULE>(1);
    return TRUE;
}

FARPROC WINAPI test_export(HMODULE module, LPCSTR name) {
    require(module == reinterpret_cast<HMODULE>(1) && !strcmp(name, "XFilesMoviePlaying"),
            "Unexpected adapter export");
    return query_present ? reinterpret_cast<FARPROC>(&test_query) : nullptr;
}

BOOL WINAPI test_release(HMODULE module) {
    require(module == reinterpret_cast<HMODULE>(1), "Released another module");
    ++released;
    return TRUE;
}
}

#define GetModuleHandleExW test_module
#define GetProcAddress test_export
#define FreeLibrary test_release
#include "../src/playback/files.cpp"
#undef FreeLibrary
#undef GetProcAddress
#undef GetModuleHandleExW

void trace_value(const char*, std::uint32_t) {}

[[noreturn]] void unsupported(std::uint32_t, const char*, std::uintptr_t) {
    throw std::runtime_error("Unexpected QuickTime dispatch");
}

namespace media {
std::filesystem::path locate_file(const std::filesystem::path& path) {
    return path;
}
}

namespace enhancements {
void stop_rumble() noexcept {}

void cancel_rumble(std::uintptr_t) noexcept {}
}

namespace playback {
struct Output::State {};

Output::~Output() = default;
Audio::~Audio() = default;

void release_callbacks(MovieHandle) {}

void refresh_time(Movie&) {
    ++advanced;
}

void sync_audio(Movie&) {
    ++advanced;
}

void task_movie(MovieHandle) {
    ++advanced;
}
}

int main() {
    try {
        using playback::scene_movie_playing;
        require(!scene_movie_playing(600, 240), "Empty movie state blocks actions");
        auto value = std::make_unique<playback::Movie>();
        auto& movie = *value;
        media::Track video;
        video.handler = "vide";
        auto track = std::make_unique<playback::Track>();
        track->media = &video;
        movie.tracks.push_back(std::move(track));
        movie.relative_path = L"XV/19650.xmv";
        movie.box = {0, 0, 240, 600};
        movie.rate = playback::unit_rate;
        movie.time = 17;
        movie.started = std::chrono::steady_clock::now();
        const auto started = movie.started;
        playback::movies.emplace(&movie.pointer, std::move(value));
        require(scene_movie_playing(600, 240), "Scene story movie is allowed");
        require(movie.time == 17 && movie.started == started && movie.rate == playback::unit_rate &&
                    !advanced,
                "Query advanced clocks or changed playback");
        movie.rate = 0;
        require(!scene_movie_playing(600, 240), "Paused or stopped movie is blocked");
        movie.rate = playback::unit_rate;
        movie.active = false;
        require(!scene_movie_playing(600, 240), "Inactive movie is blocked");
        movie.active = true;
        movie.tracks.front()->enabled = false;
        require(!scene_movie_playing(600, 240), "Disabled video is blocked");
        movie.tracks.front()->enabled = true;
        video.handler = "soun";
        require(!scene_movie_playing(600, 240), "Audio-only movie is blocked");
        video.handler = "vide";
        movie.box.right = 449;
        require(!scene_movie_playing(600, 240), "Small inset movie is blocked");
        movie.box.right = 450;
        require(scene_movie_playing(600, 240), "Three-quarter scene movie is allowed");
        movie.box.right = 600;
        for (const auto path : {L"NAV1.NMV", L"XN/navM.nmv", L"xn/000123.XmV", L"XN/9.xmv"}) {
            movie.relative_path = path;
            require(!scene_movie_playing(600, 240), "Known navigation movie is blocked");
        }
        for (const auto path : {L"nav8.nmv", L"nav1.xmv", L"XN/12a.xmv", L"XN/12.nmv", L"XV/12.xmv",
                                L"XN/.xmv", L"XN/-12.xmv"}) {
            movie.relative_path = path;
            require(scene_movie_playing(600, 240), "Non-navigation movie bypassed guard");
        }
        require(!scene_movie_playing(0, 240) && !scene_movie_playing(600, -1) &&
                    !scene_movie_playing(INT_MAX, INT_MAX),
                "Invalid or enormous scene dimensions overflowed");
        movie.box.right = -1;
        movie.box.bottom = -1;
        require(!scene_movie_playing(600, 240), "Invalid movie bounds are blocked");
        bool other_thread = true;
        std::thread([&] { other_thread = scene_movie_playing(600, 240); }).join();
        require(!other_thread, "Query inspected another movie thread");
        playback::movies.clear();
        adapter_loaded = true;
        require(!scene_movie_playing(600, 240) && queried == 1 && borrowed == released,
                "Idle DVD query blocked or leaked its module");
        dvd_playing = true;
        require(scene_movie_playing(600, 240) && borrowed == released,
                "Playing DVD bypassed guard or leaked its module");
        dvd_playing = false;
        query_present = false;
        require(scene_movie_playing(600, 240) && queried == 2 && borrowed == released,
                "Older loaded adapter bypassed conservative guard");
        require(!advanced, "Query delivered movie work");
        std::cout << "Read-only scene movie guard passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
