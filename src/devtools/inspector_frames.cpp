#include "inspector_internal.h"
#include "database/browser_accessibility.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace devtools::inspector {
namespace {
class SelectionGuard {
public:
    SelectionGuard() : previous_(state.frame_selecting) {
        state.frame_selecting = true;
    }

    ~SelectionGuard() {
        state.frame_selecting = previous_;
    }

private:
    bool previous_;
};

void enable(HWND control, bool enabled) {
    if ((IsWindowEnabled(control) != FALSE) != enabled) {
        EnableWindow(control, enabled);
    }
}

void reset_frames() {
    if (!state.frame_player && state.frame_cells.empty() && state.frame_track_indices.empty()) {
        return;
    }
    SelectionGuard selecting;
    state.frame_player = nullptr;
    state.frame_track.reset();
    state.frame_cells.clear();
    state.frame_track_indices.clear();
    SendMessageW(state.frame_tracks, CB_RESETCONTENT, 0, 0);
    ListView_SetItemCountEx(state.frame_list, 0, LVSICF_NOSCROLL);
}

void update_tracks() {
    state.frame_track_indices = state.player->video_tracks();
    SendMessageW(state.frame_tracks, CB_RESETCONTENT, 0, 0);
    for (const auto index : state.frame_track_indices) {
        const auto& track = state.player->movie().tracks[index];
        const auto label = L"Video track " + std::to_wstring(index + 1) + L" (ID " +
                           std::to_wstring(track.id) + L") | " +
                           std::to_wstring(track.samples.size()) + L" frames";
        if (SendMessageW(state.frame_tracks, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(label.c_str())) < 0) {
            throw std::runtime_error("Cannot populate preview video tracks");
        }
    }
}

void update_frames() {
    const auto count = state.player->frame_count();
    if (count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Too many frames for the preview list");
    }
    std::vector<std::wstring> cells;
    cells.reserve(count);
    for (std::size_t sample = 0; sample < count; ++sample) {
        const auto [width, height] = state.player->frame_dimensions(sample);
        cells.push_back(L"Frame " + std::to_wstring(sample + 1) +
                        (width && height
                             ? L" | " + std::to_wstring(width) + L" x " + std::to_wstring(height)
                             : L""));
    }
    SelectionGuard selecting;
    SendMessageW(state.frame_list, WM_SETREDRAW, FALSE, 0);
    ListView_SetItemState(state.frame_list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    state.frame_cells = std::move(cells);
    ListView_SetItemCountEx(state.frame_list, static_cast<int>(count), LVSICF_NOSCROLL);
    SendMessageW(state.frame_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(state.frame_list, nullptr, FALSE);
}
}

bool frame_navigation_active() {
    return state.player && state.player->frame_navigation();
}

void show_frame_controls(bool visible) {
    const auto show = visible && frame_navigation_active() ? SW_SHOW : SW_HIDE;
    for (const auto control :
         {state.frame_previous, state.frame_next, state.frame_tracks, state.frame_list}) {
        if (((GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) != (show == SW_SHOW)) {
            ShowWindow(control, show);
        }
    }
}

void create_frame_controls() {
    state.frame_previous =
        child(L"BUTTON", L"Previous frame", BS_PUSHBUTTON | WS_TABSTOP, frame_previous_id);
    state.frame_next = child(L"BUTTON", L"Next frame", BS_PUSHBUTTON | WS_TABSTOP, frame_next_id);
    state.frame_tracks = child(L"COMBOBOX", L"Video track",
                               CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, frame_tracks_id);
    state.frame_list = child(WC_LISTVIEWW, L"Preview frames",
                             WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL |
                                 LVS_SHOWSELALWAYS,
                             frame_list_id);
    database_browser::install_list_accessibility(state.frame_list);
    ListView_SetExtendedListViewStyle(state.frame_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT;
    column.pszText = const_cast<LPWSTR>(L"Frames");
    ListView_InsertColumn(state.frame_list, 0, &column);
    show_frame_controls(false);
}

void update_frame_controls() {
    if (!state.frame_list) {
        return;
    }
    if (!frame_navigation_active()) {
        reset_frames();
        show_frame_controls(false);
        return;
    }
    const auto track = state.player->video_track();
    const bool player_changed = state.frame_player != state.player.get();
    if (player_changed) {
        update_tracks();
    }
    if (player_changed || state.frame_track != track) {
        update_frames();
        const auto found = std::find(state.frame_track_indices.begin(),
                                     state.frame_track_indices.end(), track.value_or(SIZE_MAX));
        const auto row = found == state.frame_track_indices.end()
                             ? -1
                             : static_cast<int>(found - state.frame_track_indices.begin());
        SendMessageW(state.frame_tracks, CB_SETCURSEL, row, 0);
        state.frame_track = track;
        state.frame_player = state.player.get();
    }
    const auto image = state.player->image();
    const auto sample = image ? std::optional<std::size_t>(image->sample) : std::nullopt;
    enable(state.frame_previous, sample && *sample > 0);
    enable(state.frame_next, sample && *sample + 1 < state.player->frame_count());
    enable(state.frame_tracks, !state.frame_track_indices.empty());
    const auto selected = sample ? static_cast<int>(*sample) : -1;
    if (ListView_GetNextItem(state.frame_list, -1, LVNI_SELECTED) != selected) {
        SelectionGuard selecting;
        ListView_SetItemState(state.frame_list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        if (sample) {
            ListView_SetItemState(state.frame_list, selected, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(state.frame_list, selected, FALSE);
        }
    }
    show_frame_controls(!state.showing_state && !state.showing_database);
}

bool frame_command(int id, int event) {
    if (id != frame_previous_id && id != frame_next_id && id != frame_tracks_id &&
        id != frame_list_id) {
        return false;
    }
    if (id == frame_list_id || !frame_navigation_active() || state.frame_selecting) {
        return true;
    }
    if (id == frame_tracks_id) {
        if (event != CBN_SELCHANGE) {
            return true;
        }
        const auto row = SendMessageW(state.frame_tracks, CB_GETCURSEL, 0, 0);
        if (row < 0 || static_cast<std::size_t>(row) >= state.frame_track_indices.size()) {
            return true;
        }
        state.player->select_video_track(state.frame_track_indices[row]);
    } else {
        if (event != BN_CLICKED) {
            return true;
        }
        if (const auto image = state.player->image()) {
            if (id == frame_previous_id && image->sample > 0) {
                state.player->select_frame(image->sample - 1);
            } else if (id == frame_next_id && image->sample + 1 < state.player->frame_count()) {
                state.player->select_frame(image->sample + 1);
            }
        }
    }
    tick_preview();
    return true;
}

bool frame_notify(const NMHDR* notification) {
    if (!notification || notification->hwndFrom != state.frame_list) {
        return false;
    }
    if (notification->code == LVN_GETDISPINFOW) {
        auto& item = reinterpret_cast<NMLVDISPINFOW*>(const_cast<NMHDR*>(notification))->item;
        if ((item.mask & LVIF_TEXT) && item.pszText && item.cchTextMax > 0) {
            const auto* label =
                item.iItem >= 0 && item.iSubItem == 0 &&
                        static_cast<std::size_t>(item.iItem) < state.frame_cells.size()
                    ? state.frame_cells[item.iItem].c_str()
                    : L"";
            wcsncpy_s(item.pszText, static_cast<std::size_t>(item.cchTextMax), label, _TRUNCATE);
        }
    } else if (notification->code == LVN_ITEMCHANGED && !state.frame_selecting &&
               frame_navigation_active()) {
        const auto& change = *reinterpret_cast<const NMLISTVIEW*>(notification);
        if ((change.uChanged & LVIF_STATE) && (change.uNewState & LVIS_SELECTED) &&
            !(change.uOldState & LVIS_SELECTED) && change.iItem >= 0 &&
            static_cast<std::size_t>(change.iItem) < state.player->frame_count()) {
            state.player->select_frame(static_cast<std::size_t>(change.iItem));
            tick_preview();
        }
    }
    return true;
}
}
