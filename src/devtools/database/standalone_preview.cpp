#include "standalone_preview.h"
#include <commctrl.h>
#include <algorithm>
#include <stdexcept>

namespace devtools::standalone {
namespace {
void set_text(HWND window, const std::wstring& value) {
    std::wstring previous(GetWindowTextLengthW(window) + 1, L'\0');
    previous.resize(GetWindowTextW(window, previous.data(), static_cast<int>(previous.size())));
    if (previous != value) {
        SetWindowTextW(window, value.c_str());
    }
}

LRESULT CALLBACK picture_proc(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR,
                              DWORD_PTR owner) {
    auto& state = *reinterpret_cast<PreviewWindow*>(owner);
    if (state.view.scroll(window, message, value)) {
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    return DefSubclassProc(window, message, value, data);
}

HWND child(HWND parent, HMODULE module, HFONT font, const wchar_t* type, const wchar_t* caption,
           DWORD style, unsigned id = 0) {
    const auto window =
        CreateWindowExW(0, type, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module, nullptr);
    if (!window) {
        throw std::runtime_error("Cannot create preview controls");
    }
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    return window;
}

void failure(HWND owner, const std::exception& error) {
    MessageBoxA(owner, error.what(), "The X-Files Dev tools", MB_OK | MB_ICONERROR);
}
}

void preview_layout(PreviewWindow& state) {
    RECT bounds{};
    GetClientRect(state.window, &bounds);
    const auto width = bounds.right, height = bounds.bottom;
    const bool navigation = state.player && state.player->frame_navigation();
    const auto extra = navigation ? 114 : 0;
    MoveWindow(state.picture, 12, 12, std::max(1L, width - 24), std::max(1L, height - 164 - extra),
               TRUE);
    MoveWindow(state.size, 12, height - 142 - extra, 145, 160, TRUE);
    if (navigation) {
        MoveWindow(state.previous, 170, height - 142 - extra, 85, 26, TRUE);
        MoveWindow(state.next, 263, height - 142 - extra, 70, 26, TRUE);
        MoveWindow(state.tracks, 12, height - 224, width - 24, 240, TRUE);
        MoveWindow(state.frames, 12, height - 192, width - 24, 74, TRUE);
        ListView_SetColumnWidth(state.frames, 0, width - 48);
    }
    MoveWindow(state.clock, 12, height - 110, width - 24, 22, TRUE);
    MoveWindow(state.play, 12, height - 84, 100, 28, TRUE);
    MoveWindow(state.stop, 120, height - 84, 70, 28, TRUE);
    MoveWindow(state.seek, 198, height - 84, std::max(1L, width - 210), 28, TRUE);
    MoveWindow(state.caption, 12, height - 46, width - 24, 36, TRUE);
    if (state.player) {
        const auto& frame = state.player->frame();
        state.view.refresh(state.picture, frame.width, frame.height);
    }
}

void preview_tick(PreviewWindow& state) {
    if (!state.player) {
        return;
    }
    state.player->update();
    const auto time = state.player->time(), duration = state.player->duration();
    auto label = std::to_wstring(time / 60000) + L":" + (time / 1000 % 60 < 10 ? L"0" : L"") +
                 std::to_wstring(time / 1000 % 60) + L" / " + std::to_wstring(duration / 60000) +
                 L":" + (duration / 1000 % 60 < 10 ? L"0" : L"") +
                 std::to_wstring(duration / 1000 % 60);
    set_text(state.play, state.player->playing() ? L"Pause" : L"Play");
    const auto position = static_cast<LPARAM>(duration ? time * 1000 / duration : 0);
    if (SendMessageW(state.seek, TBM_GETPOS, 0, 0) != position) {
        SendMessageW(state.seek, TBM_SETPOS, TRUE, position);
    }
    set_text(state.caption, state.player->caption());
    const auto image = state.player->image();
    const auto sample = image ? std::optional<std::size_t>{image->sample} : std::nullopt;
    const auto track = image ? state.player->video_track() : std::nullopt;
    if (state.player->frame_navigation()) {
        if (state.player->direct_frame()) {
            label = L"Frame " + (sample ? std::to_wstring(*sample + 1) : L"0") + L" / " +
                    std::to_wstring(state.player->frame_count()) + L" | Direct frame preview";
        }
        EnableWindow(state.previous, sample && *sample > 0);
        EnableWindow(state.next, sample && *sample + 1 < state.player->frame_count());
        const auto selected = sample ? static_cast<int>(*sample) : -1;
        if (ListView_GetNextItem(state.frames, -1, LVNI_SELECTED) != selected) {
            state.selecting = true;
            ListView_SetItemState(state.frames, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            if (sample) {
                ListView_SetItemState(state.frames, selected, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(state.frames, selected, FALSE);
            }
            state.selecting = false;
        }
    }
    set_text(state.clock, label);
    if (state.first_tick || sample != state.painted_sample || track != state.painted_track) {
        state.first_tick = false;
        state.painted_sample = sample;
        state.painted_track = track;
        const auto& frame = state.player->frame();
        state.view.refresh(state.picture, frame.width, frame.height);
        InvalidateRect(state.picture, nullptr, FALSE);
    }
}

void paint_content(PreviewWindow& state, const DRAWITEMSTRUCT& item) {
    FillRect(item.hDC, &item.rcItem, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    if (!state.player) {
        return;
    }
    const auto& frame = state.player->frame();
    if (!frame.width || !frame.height || frame.pixels.empty()) {
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, RGB(240, 240, 240));
        auto bounds = item.rcItem;
        DrawTextW(item.hDC, state.player->has_audio() ? L"Audio preview" : L"No decoded image", -1,
                  &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    const auto bounds =
        state.view.destination(state.picture, item.rcItem, frame.width, frame.height);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(frame.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    SetStretchBltMode(item.hDC, state.view.actual ? COLORONCOLOR : HALFTONE);
    StretchDIBits(item.hDC, bounds.left, bounds.top, bounds.right - bounds.left,
                  bounds.bottom - bounds.top, 0, 0, frame.width, frame.height, frame.pixels.data(),
                  &info, DIB_RGB_COLORS, SRCCOPY);
}

void paint(PreviewWindow& state, const DRAWITEMSTRUCT& item) {
    const auto width = item.rcItem.right - item.rcItem.left;
    const auto height = item.rcItem.bottom - item.rcItem.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    const auto dc = CreateCompatibleDC(item.hDC);
    const auto bitmap = CreateCompatibleBitmap(item.hDC, width, height);
    if (dc && bitmap) {
        const auto previous = SelectObject(dc, bitmap);
        auto buffered = item;
        buffered.hDC = dc;
        buffered.rcItem = {0, 0, width, height};
        paint_content(state, buffered);
        BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, width, height, dc, 0, 0, SRCCOPY);
        SelectObject(dc, previous);
    }
    if (bitmap) {
        DeleteObject(bitmap);
    }
    if (dc) {
        DeleteDC(dc);
    }
}

LRESULT CALLBACK preview_proc(HWND window, UINT message, WPARAM value, LPARAM data) {
    auto* state = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<PreviewWindow*>(reinterpret_cast<CREATESTRUCTW*>(data)->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) {
        return DefWindowProcW(window, message, value, data);
    }
    try {
        if (message == WM_CREATE) {
            state->picture =
                child(window, state->module, state->font, L"STATIC", L"", SS_OWNERDRAW);
            SetWindowSubclass(state->picture, picture_proc, 1, reinterpret_cast<DWORD_PTR>(state));
            state->clock = child(window, state->module, state->font, L"STATIC", L"", SS_LEFT);
            state->play = child(window, state->module, state->font, L"BUTTON", L"Play",
                                BS_PUSHBUTTON | WS_TABSTOP, play_id);
            state->stop = child(window, state->module, state->font, L"BUTTON", L"Stop",
                                BS_PUSHBUTTON | WS_TABSTOP, stop_id);
            state->seek = child(window, state->module, state->font, TRACKBAR_CLASSW, L"",
                                TBS_HORZ | TBS_NOTICKS | WS_TABSTOP);
            SendMessageW(state->seek, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
            state->caption = child(window, state->module, state->font, L"STATIC", L"", SS_CENTER);
            state->size = child(window, state->module, state->font, L"COMBOBOX", L"",
                                CBS_DROPDOWNLIST | WS_TABSTOP, size_id);
            SendMessageW(state->size, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Fit"));
            SendMessageW(state->size, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Actual size"));
            SendMessageW(state->size, CB_SETCURSEL, 0, 0);
            if (state->player && state->player->frame_navigation()) {
                state->tracks = child(window, state->module, state->font, L"COMBOBOX", L"",
                                      CBS_DROPDOWNLIST | WS_TABSTOP, tracks_id);
                for (const auto index : state->player->video_tracks()) {
                    const auto& track = state->player->movie().tracks[index];
                    const auto label = L"Video track " + std::to_wstring(index + 1) + L" (ID " +
                                       std::to_wstring(track.id) + L") | " +
                                       std::to_wstring(track.samples.size()) + L" frames";
                    const auto row = SendMessageW(state->tracks, CB_ADDSTRING, 0,
                                                  reinterpret_cast<LPARAM>(label.c_str()));
                    SendMessageW(state->tracks, CB_SETITEMDATA, row, static_cast<LPARAM>(index));
                    if (state->player->video_track() == index) {
                        SendMessageW(state->tracks, CB_SETCURSEL, row, 0);
                    }
                }
                state->previous = child(window, state->module, state->font, L"BUTTON", L"Previous",
                                        BS_PUSHBUTTON | WS_TABSTOP, previous_id);
                state->next = child(window, state->module, state->font, L"BUTTON", L"Next",
                                    BS_PUSHBUTTON | WS_TABSTOP, next_id);
                state->frames = child(window, state->module, state->font, WC_LISTVIEWW, L"",
                                      LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL |
                                          LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP,
                                      frames_id);
                ListView_SetExtendedListViewStyle(state->frames,
                                                  LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
                LVCOLUMNW column{};
                column.mask = LVCF_TEXT;
                column.pszText = const_cast<LPWSTR>(L"Frames");
                ListView_InsertColumn(state->frames, 0, &column);
                ListView_SetItemCount(state->frames,
                                      static_cast<int>(state->player->frame_count()));
            }
            EnableWindow(state->size, state->player && state->player->has_video());
            SetTimer(window, 1, 40, nullptr);
            preview_tick(*state);
            return 0;
        }
        if (message == WM_SIZE) {
            preview_layout(*state);
            return 0;
        }
        if (message == WM_GETMINMAXINFO) {
            auto* limits = reinterpret_cast<MINMAXINFO*>(data);
            limits->ptMinTrackSize = {420, state->frames ? 420 : 340};
            return 0;
        }
        if (message == WM_TIMER) {
            preview_tick(*state);
            return 0;
        }
        if (message == WM_DRAWITEM) {
            paint(*state, *reinterpret_cast<DRAWITEMSTRUCT*>(data));
            return TRUE;
        }
        if (message == WM_COMMAND && state->player) {
            if (LOWORD(value) == play_id) {
                if (state->player->playing()) {
                    state->player->pause();
                } else {
                    state->player->play();
                }
            } else if (LOWORD(value) == stop_id) {
                state->player->pause();
                state->player->seek(0);
            } else if (LOWORD(value) == size_id && HIWORD(value) == CBN_SELCHANGE) {
                state->view.actual = SendMessageW(state->size, CB_GETCURSEL, 0, 0) == 1;
                state->view.reset(state->picture);
                const auto& frame = state->player->frame();
                state->view.refresh(state->picture, frame.width, frame.height);
                InvalidateRect(state->picture, nullptr, FALSE);
            } else if (LOWORD(value) == tracks_id && HIWORD(value) == CBN_SELCHANGE &&
                       state->tracks) {
                const auto row = SendMessageW(state->tracks, CB_GETCURSEL, 0, 0);
                const auto index = SendMessageW(state->tracks, CB_GETITEMDATA, row, 0);
                if (row != CB_ERR && index != CB_ERR) {
                    state->player->select_video_track(static_cast<std::size_t>(index));
                    state->selecting = true;
                    ListView_SetItemState(state->frames, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_SetItemCount(state->frames,
                                          static_cast<int>(state->player->frame_count()));
                    state->selecting = false;
                    state->view.reset(state->picture);
                }
            } else if (LOWORD(value) == previous_id || LOWORD(value) == next_id) {
                if (const auto image = state->player->image()) {
                    if (LOWORD(value) == previous_id && image->sample) {
                        state->player->select_frame(image->sample - 1);
                    } else if (LOWORD(value) == next_id && image->sample + 1 < image->count) {
                        state->player->select_frame(image->sample + 1);
                    }
                }
            }
            preview_tick(*state);
            return 0;
        }
        if (message == WM_NOTIFY && state->player && state->frames) {
            const auto* notice = reinterpret_cast<NMHDR*>(data);
            if (notice->hwndFrom == state->frames && notice->code == LVN_ITEMCHANGED &&
                !state->selecting) {
                const auto selected = ListView_GetNextItem(state->frames, -1, LVNI_SELECTED);
                if (selected >= 0) {
                    state->player->select_frame(static_cast<std::size_t>(selected));
                    preview_tick(*state);
                }
            } else if (notice->hwndFrom == state->frames && notice->code == LVN_GETDISPINFOW) {
                auto& item = reinterpret_cast<NMLVDISPINFOW*>(data)->item;
                if ((item.mask & LVIF_TEXT) && item.iItem >= 0 &&
                    static_cast<std::size_t>(item.iItem) < state->player->frame_count() &&
                    item.pszText && item.cchTextMax > 0) {
                    const auto [width, height] = state->player->frame_dimensions(item.iItem);
                    const auto label = L"Frame " + std::to_wstring(item.iItem + 1) +
                                       (width && height ? L" | " + std::to_wstring(width) + L" x " +
                                                              std::to_wstring(height)
                                                        : L"");
                    wcsncpy_s(item.pszText, static_cast<std::size_t>(item.cchTextMax),
                              label.c_str(), _TRUNCATE);
                }
            }
            return 0;
        }
        if (message == WM_HSCROLL && reinterpret_cast<HWND>(data) == state->seek && state->player) {
            const auto position = SendMessageW(state->seek, TBM_GETPOS, 0, 0);
            state->player->seek(state->player->duration() * static_cast<std::uint64_t>(position) /
                                1000);
            preview_tick(*state);
            return 0;
        }
        if (message == WM_DESTROY) {
            KillTimer(window, 1);
            state->player.reset();
            state->window = nullptr;
            return 0;
        }
    } catch (const std::exception& error) {
        state->player.reset();
        KillTimer(window, 1);
        EnableWindow(state->play, FALSE);
        EnableWindow(state->stop, FALSE);
        EnableWindow(state->seek, FALSE);
        failure(window, error);
        return message == WM_CREATE ? -1 : 0;
    }
    return DefWindowProcW(window, message, value, data);
}

}
