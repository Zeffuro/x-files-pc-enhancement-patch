#include "movie.h"
#include "caption_text.h"
#include "caption_paint.h"
#include "quickdraw/world.h"
#include "settings.h"

#include <algorithm>
#include <stdexcept>

namespace playback {
namespace {

std::wstring caption_text(std::span<const std::uint8_t> packet) {
    if (packet.size() < 2) {
        throw std::runtime_error("Truncated caption sample");
    }
    const int length = (packet[0] << 8) | packet[1];
    if (static_cast<std::size_t>(length) > packet.size() - 2) {
        throw std::runtime_error("Caption text exceeds sample size");
    }
    if (!length) {
        return {};
    }
    const auto text = reinterpret_cast<const char*>(packet.data() + 2);
    const auto count = MultiByteToWideChar(10000, 0, text, length, nullptr, 0);
    if (!count) {
        throw std::runtime_error("Cannot decode Macintosh caption text");
    }
    std::wstring result(count, L'\0');
    MultiByteToWideChar(10000, 0, text, length, result.data(), count);
    if (result.find_first_not_of(L" \r\n\t") == std::wstring::npos) {
        return {};
    }
    return normalize_caption(result);
}

}

std::wstring current_caption(const Movie& value) {
    return current_caption(value, settings().captions);
}

std::wstring current_caption(const Movie& value, CaptionMode mode) {
    if (mode == CaptionMode::On && !value.source_path.empty() &&
        std::any_of(value.tracks.begin(), value.tracks.end(), [](const auto& track) {
            return track->enabled && track->media->handler == "vide";
        })) {
        std::vector<wchar_t> executable(32768);
        const auto length =
            GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length && length < executable.size()) {
            const auto game_root = std::filesystem::path(executable.data()).parent_path();
            std::error_code error;
            const auto manifest = game_root / L"subtitles" / L"manifest.tsv";
            std::optional<std::filesystem::file_time_type> stamp;
            if (std::filesystem::is_regular_file(manifest, error) && !error) {
                stamp = std::filesystem::last_write_time(manifest, error);
                if (error) {
                    stamp.reset();
                }
            }
            const auto generation = media::subtitles::install_generation();
            if (!value.override_checked || stamp != value.override_stamp ||
                generation != value.override_generation) {
                value.override_checked = true;
                value.override_stamp = stamp;
                value.override_generation = generation;
                value.override_cues = stamp ? media::subtitles::load_override(
                                                  game_root, value.relative_path, value.source_path)
                                            : std::nullopt;
            }
            if (value.override_cues) {
                return media::subtitles::caption_at(
                    *value.override_cues, static_cast<std::uint64_t>(std::max(0, value.time)),
                    value.media->timescale);
            }
        }
    }

    struct Caption {
        const media::Track* track;
        std::wstring text;
    };

    std::vector<Caption> captions;
    bool has_video = false;
    for (const auto& track : value.tracks) {
        has_video |= track->enabled && track->media->handler == "vide";
        if (track->media->handler != "text" || mode == CaptionMode::Off ||
            (mode == CaptionMode::Game && !track->enabled)) {
            continue;
        }
        const auto sample = track->media->sample_at(value.time, value.media->timescale);
        if (!sample) {
            continue;
        }
        auto line = caption_text(value.media->packet(track->media->samples[*sample]));
        if (!line.empty()) {
            const auto previous =
                std::find_if(captions.begin(), captions.end(), [&](const auto& caption) {
                    return caption.track->placement == track->media->placement;
                });
            // Edited movies can overlay corrected dialogue on an earlier text track.
            if (previous == captions.end()) {
                captions.push_back({track->media, std::move(line)});
            } else if (track->media->layer <= previous->track->layer) {
                *previous = {track->media, std::move(line)};
            }
        }
    }
    std::wstring text;
    for (const auto& caption : captions) {
        if (!text.empty()) {
            text += L'\n';
        }
        text += caption.text;
    }
    return has_video ? text : std::wstring{};
}

bool draw_captions(Movie& value, std::wstring text, const CaptionLayout& geometry,
                   const CaptionStyle& style, bool video_changed) {
    if (text.empty() && !value.caption_bounds) {
        value.caption_style = style;
        return false;
    }
    if (!video_changed && !value.redraw && text == value.caption) {
        return false;
    }
    const auto dc = quickdraw::port_dc(value.port);
    if (!dc) {
        return false;
    }
    RECT clip{};
    if (GetClipBox(dc, &clip) == ERROR) {
        throw std::runtime_error("Cannot obtain caption drawing bounds");
    }
    RECT area{
        std::max<LONG>({geometry.caption.left, value.port->bounds.left, clip.left}),
        std::max<LONG>({geometry.caption.top, value.port->bounds.top, clip.top}),
        std::min<LONG>({geometry.caption.right, value.port->bounds.right, clip.right}),
        std::min<LONG>({geometry.caption.bottom, value.port->bounds.bottom, clip.bottom}),
    };
    if (area.right - area.left <= 8 || area.bottom <= area.top) {
        return false;
    }
    area = paint_caption(dc, area, text, style, value.box.right - value.box.left);
    GdiFlush();
    const quickdraw::Rect bounds{
        static_cast<std::int16_t>(area.top), static_cast<std::int16_t>(area.left),
        static_cast<std::int16_t>(area.bottom), static_cast<std::int16_t>(area.right)};
    value.caption = std::move(text);
    value.caption_style = style;
    value.caption_bounds = value.caption.empty() ? std::nullopt : std::optional(bounds);
    return true;
}

}
