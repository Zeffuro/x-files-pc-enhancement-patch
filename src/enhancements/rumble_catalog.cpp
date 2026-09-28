#include "rumble_catalog.h"
#include "rumble_event.h"
#include "rumble_catalog_generated.h"

namespace enhancements::rumble {

Effect clip_effect(std::wstring_view path) noexcept {
    for (const auto& clip : catalog::clips) {
        if (asset_matches(path, clip.path)) {
            return clip.effect;
        }
    }
    return {};
}

Effect short_effect() noexcept {
    return catalog::ps1_short;
}

Effect start_effect(std::wstring_view path, std::int32_t time, std::int32_t prior_rate,
                    std::int32_t rate) noexcept {
    if (time != 0 || prior_rate != 0 || rate != (1 << 16)) {
        return {};
    }
    return gunfire_asset(path) ? short_effect() : clip_effect(path);
}

Effect PlaybackCue::start(std::wstring_view path, std::int32_t time, std::int32_t prior_rate,
                          std::int32_t rate) noexcept {
    if (started_) {
        return {};
    }
    const auto effect = start_effect(path, time, prior_rate, rate);
    started_ = effect.duration_ms != 0;
    return effect;
}

void PlaybackCue::seek(std::int32_t time) noexcept {
    started_ = time != 0;
}

}
