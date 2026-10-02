#pragma once
#include <windows.h>
#include <algorithm>

namespace devtools {
struct ImageView {
    bool actual = false;

    void refresh(HWND window, unsigned width, unsigned height) const {
        // Recheck after bars appear because each bar reduces the other axis.
        for (unsigned pass = 0; pass < 3; ++pass) {
            RECT bounds{};
            GetClientRect(window, &bounds);
            for (const auto axis : {SB_HORZ, SB_VERT}) {
                const auto extent = axis == SB_HORZ ? width : height;
                const auto page = axis == SB_HORZ ? bounds.right : bounds.bottom;
                SCROLLINFO info{sizeof(info)};
                info.fMask = SIF_RANGE | SIF_PAGE;
                info.nMin = 0;
                info.nMax = actual && extent ? static_cast<int>(extent - 1) : 0;
                info.nPage = static_cast<UINT>(std::max(1L, page));
                SetScrollInfo(window, axis, &info, TRUE);
            }
        }
    }

    void reset(HWND window) const {
        SetScrollPos(window, SB_HORZ, 0, TRUE);
        SetScrollPos(window, SB_VERT, 0, TRUE);
    }

    bool scroll(HWND window, UINT message, WPARAM value) const {
        if (!actual ||
            (message != WM_HSCROLL && message != WM_VSCROLL && message != WM_MOUSEWHEEL)) {
            return false;
        }
        const auto axis = message == WM_HSCROLL ? SB_HORZ : SB_VERT;
        SCROLLINFO info{sizeof(info)};
        info.fMask = SIF_ALL;
        GetScrollInfo(window, axis, &info);
        auto position = info.nPos;
        if (message == WM_MOUSEWHEEL) {
            position -= GET_WHEEL_DELTA_WPARAM(value) / WHEEL_DELTA * 48;
        } else {
            switch (LOWORD(value)) {
                case SB_LINEUP:
                    position -= 16;
                    break;
                case SB_LINEDOWN:
                    position += 16;
                    break;
                case SB_PAGEUP:
                    position -= static_cast<int>(info.nPage);
                    break;
                case SB_PAGEDOWN:
                    position += static_cast<int>(info.nPage);
                    break;
                case SB_TOP:
                    position = info.nMin;
                    break;
                case SB_BOTTOM:
                    position = info.nMax;
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    position = info.nTrackPos;
                    break;
                default:
                    return true;
            }
        }
        SetScrollPos(window, axis, position, TRUE);
        if (GetScrollPos(window, axis) != info.nPos) {
            InvalidateRect(window, nullptr, FALSE);
        }
        return true;
    }

    RECT destination(HWND window, const RECT& bounds, unsigned width, unsigned height) const {
        const auto available_width = bounds.right - bounds.left;
        const auto available_height = bounds.bottom - bounds.top;
        const auto scale =
            actual ? 1.0
                   : std::min(double(available_width) / width, double(available_height) / height);
        const auto w = std::max(1, static_cast<int>(width * scale));
        const auto h = std::max(1, static_cast<int>(height * scale));
        const auto x = bounds.left + (actual && w > available_width ? -GetScrollPos(window, SB_HORZ)
                                                                    : (available_width - w) / 2);
        const auto y = bounds.top + (actual && h > available_height ? -GetScrollPos(window, SB_VERT)
                                                                    : (available_height - h) / 2);
        return {x, y, x + w, y + h};
    }
};
}
