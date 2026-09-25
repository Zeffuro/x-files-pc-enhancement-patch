#include "movie.h"
#include "caption_text.h"
#include "quickdraw/world.h"
#include "settings.h"

#include <algorithm>
#include <stdexcept>

namespace playback {
namespace {

const wchar_t* caption_font(CaptionFont selected) {
    static const auto loaded = [] {
        std::array<bool, caption_fonts.size()> result{true};
        std::vector<wchar_t> path(32768);
        const auto length =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length && length < path.size()) {
            const auto folder = std::filesystem::path(path.data()).parent_path();
            for (std::size_t i = 1; i < caption_fonts.size(); ++i) {
                result[i] = AddFontResourceExW((folder / caption_fonts[i].file).c_str(), FR_PRIVATE,
                                               nullptr) != 0;
            }
        }
        return result;
    }();
    const auto index = static_cast<std::size_t>(selected);
    return caption_fonts[index < loaded.size() && loaded[index] ? index : 0].name;
}

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
    struct Caption {
        const media::Track* track;
        std::wstring text;
    };

    std::vector<Caption> captions;
    bool has_video = false;
    const auto mode = settings().captions;
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

bool draw_captions(Movie& value, std::wstring text, bool video_changed) {
    if (text.empty() && !value.caption_bounds) {
        value.caption_style = settings().caption_style;
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
        std::max<LONG>({value.box.left, value.port->bounds.left, clip.left}),
        std::max<LONG>({value.box.top, value.port->bounds.top, clip.top}),
        std::min<LONG>({value.box.right, value.port->bounds.right, clip.right}),
        std::min<LONG>({value.box.bottom, value.port->bounds.bottom, clip.bottom}),
    };
    if (area.right - area.left <= 8 || area.bottom <= area.top) {
        return false;
    }
    const auto saved = SaveDC(dc);
    const auto style = settings().caption_style;
    const int font_height =
        std::max(12, MulDiv(value.box.right - value.box.left, 18, 600)) * style.scale / 100;
    const auto font = CreateFontW(-font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH, caption_font(style.font));
    if (!saved || !font) {
        if (saved) {
            RestoreDC(dc, saved);
        }
        if (font) {
            DeleteObject(font);
        }
        throw std::runtime_error("Cannot create caption drawing context");
    }
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    auto layout = area;
    layout.left += 4;
    layout.right -= 4;
    constexpr UINT format = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX;
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &layout, format | DT_CALCRECT);
    const int height = std::max<LONG>(font_height, layout.bottom - layout.top) + 4;
    area.top = std::max(area.top, area.bottom - height);
    layout = area;
    layout.left += 4;
    layout.right -= 4;
    layout.top += 2;
    auto shadow = layout;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &shadow, format);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &layout, format);
    GdiFlush();
    RestoreDC(dc, saved);
    DeleteObject(font);
    const quickdraw::Rect bounds{
        static_cast<std::int16_t>(area.top), static_cast<std::int16_t>(area.left),
        static_cast<std::int16_t>(area.bottom), static_cast<std::int16_t>(area.right)};
    value.caption = std::move(text);
    value.caption_style = style;
    value.caption_bounds = value.caption.empty() ? std::nullopt : std::optional(bounds);
    return true;
}

}
