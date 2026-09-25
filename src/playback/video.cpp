#include "movie.h"
#include "menu_colors.h"
#include "settings.h"
#include "quickdraw/world.h"

#include <stdexcept>

namespace playback {

bool draw_movie(Movie& value) {
    bool changed = false;
    try {
        auto caption = current_caption(value);
        const bool caption_changed =
            caption != value.caption || value.caption_style != settings().caption_style;
        for (auto& track : value.tracks) {
            if (!track->enabled || track->media->handler != "vide") {
                continue;
            }
            const auto duration = value.media->duration;
            const auto time = duration && static_cast<std::uint64_t>(value.time) == duration
                                  ? duration - 1
                                  : static_cast<std::uint64_t>(value.time);
            const auto sample = track->media->sample_at(time, value.media->timescale);
            if (!sample || (!value.redraw && !caption_changed && track->displayed == sample)) {
                continue;
            }
            if (!track->video) {
                track->video = std::make_unique<media::Video>();
            }
            const auto& frame = track->video->decode(*value.media, *track->media, *sample);
            auto pixels = frame.pixels.data();
            std::vector<std::uint8_t> corrected;
            if (settings().menu_black_background && is_menu_animation(value.filename)) {
                corrected = frame.pixels;
                restore_menu_black(corrected);
                pixels = corrected.data();
            }
            const auto dc = quickdraw::port_dc(value.port);
            if (!dc) {
                throw std::runtime_error("Movie has no drawing port");
            }
            BITMAPINFO format{};
            format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            format.bmiHeader.biWidth = static_cast<LONG>(frame.width);
            format.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
            format.bmiHeader.biPlanes = 1;
            format.bmiHeader.biBitCount = 32;
            format.bmiHeader.biCompression = BI_RGB;
            const auto& box = value.box;
            const auto result = StretchDIBits(dc, box.left, box.top, box.right - box.left,
                                              box.bottom - box.top, 0, 0, frame.width, frame.height,
                                              pixels, &format, DIB_RGB_COLORS, SRCCOPY);
            if (result == GDI_ERROR) {
                throw std::runtime_error("Movie frame drawing failed");
            }
            trace_movie("frame", value, static_cast<std::int32_t>(*sample));
            track->displayed = sample;
            changed = true;
        }
        changed |= draw_captions(value, std::move(caption), changed);
        if (changed) {
            GdiFlush();
            quickdraw::present_port(value.port, value.box);
        }
        value.redraw = false;
    } catch (const std::exception& error) {
        unsupported(Selector::MoviesTask, error.what(), 0);
    }
    return changed;
}

}
