#include "movie.h"
#include "menu_colors.h"
#include "grading.h"
#include "settings.h"
#include "quickdraw/world.h"
#include "game/render/caption_surface.h"

#include <algorithm>
#include <stdexcept>

namespace playback {

bool draw_movie(Movie& value) {
    bool changed = false;
    try {
        auto caption = current_caption(value);
        const auto style = settings().caption_style;
        const auto dc = quickdraw::port_dc(value.port);
        RECT clip{value.box.left, value.box.top, value.box.right, value.box.bottom};
        if (dc && GetClipBox(dc, &clip) == ERROR) {
            throw std::runtime_error("Cannot obtain movie drawing bounds");
        }
        const auto layout = layout_captions(
            value.box, dc && value.port ? value.port->bounds : value.box, clip, style);
        const auto same_rect = [](const quickdraw::Rect& left, const quickdraw::Rect& right) {
            return left.top == right.top && left.left == right.left &&
                   left.bottom == right.bottom && left.right == right.right;
        };
        const bool layout_changed = !value.caption_layout ||
                                    !same_rect(value.caption_layout->image, layout.image) ||
                                    !same_rect(value.caption_layout->caption, layout.caption) ||
                                    value.caption_layout->below != layout.below;
        const bool caption_changed =
            caption != value.caption || value.caption_style != style || layout_changed;
        const auto mode = settings().movie_contrast;
        const bool grade_changed = value.displayed_contrast != mode;
        for (auto& track : value.tracks) {
            if (!track->enabled || track->media->handler != "vide") {
                continue;
            }
            const auto duration = value.media->duration;
            const auto time = duration && static_cast<std::uint64_t>(value.time) == duration
                                  ? duration - 1
                                  : static_cast<std::uint64_t>(value.time);
            const auto sample = track->media->sample_at(time, value.media->timescale);
            if (!sample || (!value.redraw && !caption_changed && !grade_changed &&
                            track->displayed == sample)) {
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
                clean_menu_colors(corrected, frame.width, frame.height);
                pixels = corrected.data();
            }
            const auto& format_description =
                track->media->descriptions.at(track->media->samples.at(*sample).description);
            const auto relative = value.relative_path.generic_string();
            if (settings().menu_black_background &&
                is_credit_pages(relative, format_description.codec, frame.width, frame.height,
                                track->media->samples.size())) {
                corrected = frame.pixels;
                clean_credit_colors(corrected);
                pixels = corrected.data();
            }
            const auto grade =
                movie_grade(mode, relative, format_description.codec, track->media->samples.size());
            if (grade != Grade{}) {
                corrected = frame.pixels;
                apply_grade(corrected, grade);
                pixels = corrected.data();
            }
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
            if (layout.below) {
                const RECT box{value.box.left, value.box.top, value.box.right, value.box.bottom};
                FillRect(dc, &box, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            }
            const auto& image = layout.image;
            const auto result = StretchDIBits(
                dc, image.left, image.top, image.right - image.left, image.bottom - image.top, 0, 0,
                frame.width, frame.height, pixels, &format, DIB_RGB_COLORS, SRCCOPY);
            if (result == GDI_ERROR) {
                throw std::runtime_error("Movie frame drawing failed");
            }
            trace_movie("frame", value, static_cast<std::int32_t>(*sample));
            value.last_frame_draw = GetTickCount64();
            track->displayed = sample;
            value.last_drawn_track = track.get();
            changed = true;
        }
        changed |= draw_captions(value, std::move(caption), layout, style, changed);
        if (changed) {
            std::optional<RECT> caption_area;
            if (value.caption_bounds) {
                const auto& area = *value.caption_bounds;
                caption_area = RECT{area.left, area.top, area.right, area.bottom};
            }
            native_game::caption_surface::paint(
                dc, {value.box.left, value.box.top, value.box.right, value.box.bottom},
                caption_area);
            GdiFlush();
            quickdraw::present_port(value.port, value.box);
        }
        value.caption_layout = layout;
        value.displayed_contrast = mode;
        value.redraw = false;
    } catch (const std::exception& error) {
        unsupported(Selector::MoviesTask, error.what(), 0);
    }
    return changed;
}

}
