#pragma once

#include "media/subtitles.h"
#include <windows.h>

namespace transcript {

inline void observe_dvd_caption(const std::filesystem::path& path,
                                const std::vector<media::subtitles::Cue>& cues,
                                const std::vector<media::subtitles::Cue>& source,
                                std::uint32_t scale, std::int64_t time) {
    using Observe =
        void(__cdecl*)(const wchar_t*, std::uint64_t, std::uint64_t, std::uint32_t, const wchar_t*);
    const auto module = GetModuleHandleW(L"QuickTime.qts");
    const auto observe =
        module ? reinterpret_cast<Observe>(GetProcAddress(module, "XFilesTranscriptCaptionV1"))
               : nullptr;
    if (!observe || time < 0 || cues.size() != source.size()) {
        return;
    }
    for (std::size_t i = 0; i < cues.size(); ++i) {
        const auto& cue = cues[i];
        if (cue.begin > static_cast<std::uint64_t>(time)) {
            break;
        }
        if (static_cast<std::uint64_t>(time) < cue.end) {
            observe(path.c_str(), source[i].begin, source[i].end, scale, cue.text.c_str());
        }
    }
}

}
