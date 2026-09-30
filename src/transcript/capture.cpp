#include "capture.h"
#include "enhancements/game_ui.h"
#include "enhancements/dialogue.h"
#include "playback/movie.h"

#include <cwctype>
#include <span>
#include <unordered_map>

namespace transcript {
namespace {
History captured;
thread_local std::unordered_map<std::uint64_t, std::vector<media::subtitles::Cue>> native;

bool gameplay_active() {
    if (!settings().dialogue_transcript) {
        return false;
    }
    const auto image = enhancements::game::executable_image();
    const auto& profile = enhancements::game::edition();
    // The native session flag is set by the menu, so it cannot gate new-game dialogue.
    return image && *reinterpret_cast<const int*>(image + profile.scene_active) != 0 &&
           enhancements::game::input_vtable() != profile.main_menu;
}

std::wstring movie_key(const std::filesystem::path& path) {
    auto key = path.generic_wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return std::towlower(c); });
    return key;
}

void observe(const std::filesystem::path& path, std::span<const media::subtitles::Cue> cues,
             std::uint64_t time, std::uint32_t scale) {
    for (const auto& cue : cues) {
        if (cue.begin > time) {
            break;
        }
        if (time < cue.end) {
            captured.record_caption(movie_key(path), cue.begin, cue.end, scale, cue.text);
        }
    }
}
}

const History& history() {
    return captured;
}

void record_choice(std::wstring text) noexcept {
    try {
        if (gameplay_active()) {
            captured.record_choice(std::move(text));
        }
    } catch (...) {
    }
}

void record_marker(std::wstring text) noexcept {
    try {
        if (settings().dialogue_transcript) {
            captured.record_marker(std::move(text));
        }
    } catch (...) {
    }
}

void observe_movie(playback::Movie& movie) noexcept {
    try {
        if (!gameplay_active() || !movie.active || !movie.rate || !movie.port ||
            !movie.last_frame_draw || movie.time < 0 || movie.relative_path.empty() ||
            movie_key(movie.relative_path.parent_path()) != L"xv") {
            return;
        }
        const bool moving =
            std::any_of(movie.tracks.begin(), movie.tracks.end(), [](const auto& t) {
                return t->enabled && t->media->handler == "vide" && t->media->samples.size() > 1;
            });
        if (!moving) {
            return;
        }
        enhancements::finish_dialogue_click(0);
        // Resolve packs even when on-screen captions are disabled.
        playback::current_caption(movie, CaptionMode::On);
        if (movie.override_cues) {
            observe(movie.relative_path, *movie.override_cues,
                    static_cast<std::uint64_t>(movie.time) * 1000 / movie.media->timescale, 1000);
            return;
        }
        if (!native.contains(movie.inspection_id)) {
            if (native.size() >= 128) {
                native.clear();
            }
            native.emplace(movie.inspection_id, media::subtitles::native_cues(*movie.media));
        }
        observe(movie.relative_path, native.at(movie.inspection_id), movie.time,
                movie.media->timescale);
    } catch (...) {
    }
}

}

extern "C" void __cdecl XFilesTranscriptCaptionV1(const wchar_t* path, std::uint64_t begin,
                                                  std::uint64_t end, std::uint32_t scale,
                                                  const wchar_t* text) noexcept {
    try {
        if (path && text && transcript::gameplay_active()) {
            enhancements::finish_dialogue_click(0);
            transcript::captured.record_caption(transcript::movie_key(path), begin, end, scale,
                                                text);
        }
    } catch (...) {
    }
}
