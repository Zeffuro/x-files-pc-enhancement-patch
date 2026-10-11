#include "inspector_internal.h"
#include <windowsx.h>
#include <algorithm>

namespace devtools::inspector {
namespace {
int preview_limit(int height) {
    return std::max(100, height - 512 - (frame_navigation_active() ? 112 : 0));
}

void resize_pane(HWND window, int position) {
    RECT bounds{};
    GetClientRect(state.window, &bounds);
    if (bounds.right < 840) {
        return;
    }
    if (window == state.column_splitter) {
        state.column_fraction =
            static_cast<double>(std::clamp(position, 420, static_cast<int>(bounds.right) - 420)) /
            bounds.right;
    } else {
        const auto limit = preview_limit(bounds.bottom);
        state.preview_fraction =
            limit > 100
                ? static_cast<double>(std::clamp(position, 100, limit) - 100) / (limit - 100)
                : 0;
    }
    layout();
}

LRESULT CALLBACK splitter_proc(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR,
                               DWORD_PTR) {
    const bool column = window == state.column_splitter;
    if (message == WM_GETDLGCODE) {
        return DefSubclassProc(window, message, value, data) | DLGC_WANTARROWS;
    }
    if (message == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, column ? IDC_SIZEWE : IDC_SIZENS));
        return TRUE;
    }
    if (message == WM_LBUTTONDOWN) {
        state.dragging_splitter = window;
        state.splitter_start = {GET_X_LPARAM(data), GET_Y_LPARAM(data)};
        MapWindowPoints(window, state.window, &state.splitter_start, 1);
        state.splitter_origin = column ? state.column_width : state.preview_height;
        SetFocus(window);
        SetCapture(window);
        return 0;
    }
    if (message == WM_MOUSEMOVE && state.dragging_splitter == window) {
        POINT point{GET_X_LPARAM(data), GET_Y_LPARAM(data)};
        MapWindowPoints(window, state.window, &point, 1);
        resize_pane(window, state.splitter_origin + (column ? point.x - state.splitter_start.x
                                                            : point.y - state.splitter_start.y));
        return 0;
    }
    if (message == WM_LBUTTONUP && state.dragging_splitter == window) {
        ReleaseCapture();
        return 0;
    }
    if (message == WM_CAPTURECHANGED) {
        state.dragging_splitter = nullptr;
    }
    if (message == WM_KEYDOWN) {
        const auto current = column ? state.column_width : state.preview_height;
        if (value == static_cast<WPARAM>(column ? VK_LEFT : VK_UP) ||
            value == static_cast<WPARAM>(column ? VK_RIGHT : VK_DOWN)) {
            resize_pane(window, current + (value == VK_LEFT || value == VK_UP ? -16 : 16));
            return 0;
        }
        if (value == VK_HOME) {
            (column ? state.column_fraction : state.preview_fraction) = 0.5;
            layout();
            return 0;
        }
    }
    return DefSubclassProc(window, message, value, data);
}
}

void create_layout_controls() {
    state.column_splitter = child(L"BUTTON", L"Resize library and preview",
                                  BS_OWNERDRAW | WS_TABSTOP, column_splitter_id);
    state.preview_splitter = child(L"BUTTON", L"Resize preview and notes",
                                   BS_OWNERDRAW | WS_TABSTOP, preview_splitter_id);
    for (const auto window : {state.column_splitter, state.preview_splitter}) {
        SetWindowSubclass(window, splitter_proc, 2, 0);
    }
}

bool paint_splitter(const DRAWITEMSTRUCT& item) {
    if (item.hwndItem != state.column_splitter && item.hwndItem != state.preview_splitter) {
        return false;
    }
    FillRect(item.hDC, &item.rcItem, GetSysColorBrush(COLOR_BTNFACE));
    auto line = item.rcItem;
    if (item.hwndItem == state.column_splitter) {
        line.left = (line.left + line.right) / 2;
        line.right = line.left + 2;
        line.top += 16;
        line.bottom -= 16;
    } else {
        line.top = (line.top + line.bottom) / 2;
        line.bottom = line.top + 2;
        line.left += 16;
        line.right -= 16;
    }
    FillRect(item.hDC, &line, GetSysColorBrush(COLOR_BTNSHADOW));
    if (item.itemState & ODS_FOCUS) {
        auto focus = item.rcItem;
        InflateRect(&focus, -1, -1);
        DrawFocusRect(item.hDC, &focus);
    }
    return true;
}

void layout() {
    RECT bounds{};
    GetClientRect(state.window, &bounds);
    const int width = bounds.right, height = bounds.bottom;
    if (width < 840 || height < 400) {
        return;
    }
    const int split = std::clamp(static_cast<int>(width * state.column_fraction), 420, width - 420);
    const int right = split + 16, detail_width = width - right - 20;
    const int preview_height =
        100 + static_cast<int>((preview_limit(height) - 100) * state.preview_fraction);
    const int bottom = 66 + preview_height, extra = frame_navigation_active() ? 112 : 0;
    state.column_width = split;
    state.preview_height = preview_height;
    MoveWindow(state.live, 20, 18, 144, 34, TRUE);
    MoveWindow(state.library, 170, 18, 130, 34, TRUE);
    MoveWindow(state.game_state, 306, 18, 130, 34, TRUE);
    MoveWindow(state.database_button, 442, 18, 158, 34, TRUE);
    MoveWindow(state.database, 20, 66, width - 40, height - 120, TRUE);
    MoveWindow(state.search_label, 20, 70, 62, 24, TRUE);
    MoveWindow(state.search, 86, 66, split - 90, 30, TRUE);
    MoveWindow(state.filenames, 20, 100, 94, 24, TRUE);
    MoveWindow(state.labels, 118, 100, 85, 24, TRUE);
    MoveWindow(state.notes_search, 207, 100, 82, 24, TRUE);
    MoveWindow(state.captions, 293, 100, 100, 24, TRUE);
    MoveWindow(state.group, 20, 130, 152, 240, TRUE);
    MoveWindow(state.place, 178, 130, std::max(110, split - 318), 260, TRUE);
    MoveWindow(state.compact, split - 132, 130, 128, 28, TRUE);
    MoveWindow(state.coverage, 20, 164, split - 24, 220, TRUE);
    MoveWindow(state.list, 20, 200, split - 24, height - 254, TRUE);
    MoveWindow(state.column_splitter, split, 66, 12, height - 120, TRUE);
    layout_state_controls(width, height);
    const int title_left = std::max(right, 620);
    MoveWindow(state.title, title_left, 20, std::max(1, width - title_left - 20), 32, TRUE);
    MoveWindow(state.preview, right, 66, detail_width, preview_height, TRUE);
    MoveWindow(state.clock, right, bottom + 6, detail_width, 24, TRUE);
    MoveWindow(state.play, right, bottom + 34, 118, 30, TRUE);
    MoveWindow(state.stop, right + 124, bottom + 34, 66, 30, TRUE);
    MoveWindow(state.subtitle, right + 196, bottom + 34, 110, 30, TRUE);
    MoveWindow(state.seek, right + 312, bottom + 34, std::max(40, detail_width - 312), 30, TRUE);
    MoveWindow(state.frame_previous, right, bottom + 70, 120, 30, TRUE);
    MoveWindow(state.frame_next, right + 126, bottom + 70, 100, 30, TRUE);
    MoveWindow(state.frame_tracks, right + 232, bottom + 70, detail_width - 232, 240, TRUE);
    MoveWindow(state.frame_list, right, bottom + 106, detail_width, 70, TRUE);
    ListView_SetColumnWidth(state.frame_list, 0, detail_width - 24);
    MoveWindow(state.caption, right, bottom + 70 + extra, detail_width, 30, TRUE);
    MoveWindow(state.preview_splitter, right, bottom + 104 + extra, detail_width, 8, TRUE);
    MoveWindow(state.details, right, bottom + 116 + extra, detail_width, 90, TRUE);
    MoveWindow(state.label_heading, right, bottom + 210 + extra, detail_width, 22, TRUE);
    MoveWindow(state.label, right, bottom + 236 + extra, detail_width, 28, TRUE);
    MoveWindow(state.notes_heading, right, bottom + 268 + extra, detail_width, 22, TRUE);
    MoveWindow(state.notes, right, bottom + 294 + extra, detail_width,
               std::max(56, height - 96 - bottom - 294 - extra), TRUE);
    MoveWindow(state.save, right, height - 86, 154, 32, TRUE);
    MoveWindow(state.status, 20, height - 38, width - 40, 30, TRUE);
    const int clip_width = state.compact_view ? 128 : 222;
    ListView_SetColumnWidth(state.list, 0, clip_width);
    ListView_SetColumnWidth(state.list, 1, std::max(100, split - clip_width - 138));
    ListView_SetColumnWidth(state.list, 2, 110);
}
}
