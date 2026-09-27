#include "inspector_internal.h"
#include "diagnostics/game_context.h"
#include <algorithm>
#include <stdexcept>

namespace devtools::inspector {
int thumbnail(const media::Frame* frame, int index, bool audio = false) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 96;
    info.bmiHeader.biHeight = -54;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    const auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        throw std::runtime_error("Cannot create clip thumbnail");
    }
    auto* pixels = static_cast<std::uint32_t*>(bits);
    std::fill_n(pixels, 96 * 54, 0xffeeeeee);
    if (frame && !frame->pixels.empty()) {
        const auto scale = std::min(96.0 / frame->width, 54.0 / frame->height);
        const auto width = std::max(1u, static_cast<unsigned>(frame->width * scale));
        const auto height = std::max(1u, static_cast<unsigned>(frame->height * scale));
        std::fill_n(pixels, 96 * 54, 0xff000000);
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                const auto* source = &frame->pixels[(y * frame->height / height * frame->width +
                                                     x * frame->width / width) *
                                                    4];
                pixels[(y + (54 - height) / 2) * 96 + x + (96 - width) / 2] =
                    0xff000000u | (source[2] << 16) | (source[1] << 8) | source[0];
            }
        }
    }
    if ((!frame || frame->pixels.empty()) && audio) {
        const auto dc = CreateCompatibleDC(nullptr);
        const auto old = SelectObject(dc, bitmap);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(64, 90, 104));
        RECT rect{0, 0, 96, 54};
        DrawTextW(dc, L"Audio", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
        DeleteDC(dc);
    }
    const auto result = index < 0 ? ImageList_Add(state.images, bitmap, nullptr)
                                  : ImageList_Replace(state.images, index, bitmap, nullptr);
    DeleteObject(bitmap);
    return result;
}

void set_list_density() {
    state.shown_artwork.fill(nullptr);
    ListView_SetImageList(state.list, nullptr, LVSIL_SMALL);
    if (state.images) {
        ImageList_Destroy(state.images);
    }
    state.images = ImageList_Create(state.compact_view ? 1 : 96, state.compact_view ? 22 : 54,
                                    ILC_COLOR32, 8, 8);
    ListView_SetImageList(state.list, state.images, LVSIL_SMALL);
}

void update_artwork() {
    if (state.compact_view || state.showing_state || !state.artwork) {
        return;
    }
    const auto now = GetTickCount64();
    if (now - state.artwork_updated < 150) {
        return;
    }
    state.artwork_updated = now;
    const auto first = std::max(0, ListView_GetTopIndex(state.list));
    const auto last = std::min(static_cast<int>(state.rows.size()),
                               first + ListView_GetCountPerPage(state.list) + 2);
    std::vector<std::filesystem::path> visible;
    bool changed = false;
    for (int i = first; i < last; ++i) {
        const auto& row = state.rows[i];
        if (state.browsing) {
            visible.push_back(row.path);
            const auto art = state.artwork->get(row.path);
            auto& shown = state.shown_artwork[i % 64];
            if (art != shown) {
                thumbnail(art ? &art->frame : nullptr, i % 64, art && art->audio);
                shown = art;
                changed = true;
            }
        } else {
            const auto live =
                std::find_if(state.movies.begin(), state.movies.end(),
                             [&](const auto& movie) { return movie.id == row.movie; });
            if (live != state.movies.end()) {
                thumbnail(&live->preview, i % 64, live->audio);
                state.shown_artwork[i % 64].reset();
                changed = true;
            }
        }
    }
    state.artwork->request(visible);
    if (changed) {
        InvalidateRect(state.list, nullptr, FALSE);
    }
}

void populate() {
    const auto query = lower(text(state.search));
    diagnostics::record_tool_context(
        (state.browsing ? L"library " : L"live ") + state.selected.generic_wstring() + L" group=" +
        text(state.group) + L" place=" + text(state.place) + L" captions=" +
        std::to_wstring(SendMessageW(state.captions, BM_GETCHECK, 0, 0)));
    std::vector<Row> rows;
    auto add = [&](const std::filesystem::path& path, std::uint64_t movie) {
        const auto group = SendMessageW(state.group, CB_GETCURSEL, 0, 0);
        const auto folder = lower(path.parent_path().wstring());
        const wchar_t* groups[] = {L"", L"xn", L"xv", L"xg", L"xs", L"xt"};
        if ((group > 0 && group < 6 && folder != groups[group]) ||
            (group == 6 && !folder.empty())) {
            return;
        }
        const auto description = state.catalog.metadata(path, false);
        if ((group == 7 && !description.starts_with(L"Action choice - ")) ||
            (group == 8 && !description.starts_with(L"Emotion - "))) {
            return;
        }
        const auto place = SendMessageW(state.place, CB_GETCURSEL, 0, 0);
        const auto places = state.catalog.places(path);
        if ((place == 1 && !places.empty()) || (place > 1 && !places.contains(text(state.place)))) {
            return;
        }
        const auto coverage = SendMessageW(state.coverage, CB_GETCURSEL, 0, 0);
        if (coverage > 0) {
            const auto available = state.caption_index ? state.caption_index->coverage(path)
                                                       : CaptionCoverage::pending;
            const auto wanted = coverage == 1   ? CaptionCoverage::present
                                : coverage == 2 ? CaptionCoverage::none
                                                : CaptionCoverage::unreadable;
            if (available != wanted) {
                return;
            }
        }
        const auto note = state.annotations.get(path);
        std::wstring searchable;
        if (SendMessageW(state.filenames, BM_GETCHECK, 0, 0)) {
            searchable += path.generic_wstring();
        }
        if (SendMessageW(state.labels, BM_GETCHECK, 0, 0)) {
            searchable +=
                L" " + state.catalog.metadata(path, false) + (note ? L" " + note->label : L"");
        }
        if (SendMessageW(state.notes_search, BM_GETCHECK, 0, 0)) {
            searchable += L" " + state.catalog.comment(path) + (note ? L" " + note->notes : L"");
        }
        const bool caption_match = SendMessageW(state.captions, BM_GETCHECK, 0, 0) &&
                                   state.caption_index &&
                                   state.caption_index->contains(path, query);
        if (lower(searchable).find(query) != std::wstring::npos || caption_match) {
            rows.push_back({path, movie});
        }
    };
    if (state.browsing) {
        for (const auto& path : state.catalog.paths) {
            add(path, 0);
        }
    } else {
        for (const auto& movie : state.movies) {
            add(movie.path, movie.id);
        }
    }
    if (!state.selected.empty() && std::none_of(rows.begin(), rows.end(), [](const auto& row) {
            return row.path == state.selected && (!row.movie || row.movie == state.selected_movie);
        })) {
        if (state.dirty || (state.player && state.player->playing())) {
            rows.push_back({state.selected, state.browsing ? 0 : state.selected_movie});
        } else {
            select({});
        }
    }
    const bool changed =
        rows.size() != state.rows.size() ||
        !std::equal(rows.begin(), rows.end(), state.rows.begin(), [](const auto& a, const auto& b) {
            return a.path == b.path && a.movie == b.movie;
        });
    state.rebuilding = true;
    if (changed) {

        SendMessageW(state.list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(state.list);
        SendMessageW(state.list, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), FALSE);
        ImageList_RemoveAll(state.images);
        state.shown_artwork.fill(nullptr);
        if (!state.compact_view) {
            for (int i = 0; i < 64; ++i) {
                thumbnail(nullptr, -1);
            }
        }
        state.rows = std::move(rows);
    }
    for (std::size_t i = 0; i < state.rows.size(); ++i) {
        const auto& row = state.rows[i];
        const auto note = state.annotations.get(row.path);
        auto name = row.path.generic_wstring();
        auto label =
            note && !note->label.empty() ? note->label : state.catalog.metadata(row.path, false);
        label.resize(label.find_first_of(L"\r\n") == std::wstring::npos
                         ? label.size()
                         : label.find_first_of(L"\r\n"));
        std::replace(label.begin(), label.end(), L'\r', L' ');
        std::replace(label.begin(), label.end(), L'\n', L' ');
        const playback::MovieSnapshot* movie = nullptr;
        if (row.movie) {
            for (const auto& value : state.movies) {
                if (value.id == row.movie) {
                    movie = &value;
                }
            }
        }
        std::wstring status = movie ? activity(*movie)
                              : state.catalog.installed_keys.contains(catalog_key(row.path))
                                  ? L"Installed"
                                  : L"Missing";
        if (changed) {
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_IMAGE;
            item.iItem = static_cast<int>(i);
            item.pszText = name.data();
            item.iImage = state.compact_view ? I_IMAGENONE : static_cast<int>(i % 64);
            ListView_InsertItem(state.list, &item);
            if (row.path == state.selected && (!row.movie || row.movie == state.selected_movie)) {
                ListView_SetItemState(state.list, item.iItem, LVIS_SELECTED, LVIS_SELECTED);
            }
        }
        ListView_SetItemText(state.list, static_cast<int>(i), 1, label.data());
        ListView_SetItemText(state.list, static_cast<int>(i), 2, status.data());
        const bool selected =
            row.path == state.selected && (!row.movie || row.movie == state.selected_movie);
        ListView_SetItemState(state.list, static_cast<int>(i), selected ? LVIS_SELECTED : 0,
                              LVIS_SELECTED);
    }
    if (changed) {
        SendMessageW(state.list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(state.list, nullptr, FALSE);
    }
    if (!state.browsing) {
        InvalidateRect(state.list, nullptr, FALSE);
    }
    state.rebuilding = false;
    if (!state.dirty) {
        set_text(state.status,
                 std::to_wstring(state.rows.size()) + L" results. " + state.catalog.status);
    }
    update_artwork();
    if (state.selected.empty() && !state.rows.empty()) {
        select(state.rows.front());
        ListView_SetItemState(state.list, 0, LVIS_SELECTED, LVIS_SELECTED);
    }
}

}
