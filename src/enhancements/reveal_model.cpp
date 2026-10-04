#include "reveal_model.h"
#include "localization/ui.h"
#include <algorithm>
#include <cstdlib>
#include <cwchar>

namespace enhancements::reveal {
namespace {
POINT center(RECT bounds) {
    return {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
}

void line(HDC dc, LONG x, LONG y, LONG dx, LONG dy) {
    MoveToEx(dc, x, y, nullptr);
    LineTo(dc, x + dx, y + dy);
}

void symbol(HDC dc, RECT r, game::Interaction interaction) {
    const auto p = center(r);
    const auto radius_x = std::min(p.x - r.left, r.right - 1 - p.x);
    const auto radius_y = std::min(p.y - r.top, r.bottom - 1 - p.y);
    if (interaction == game::Interaction::item) {
        const auto x = std::min<LONG>(7, radius_x), y = std::min<LONG>(6, radius_y);
        line(dc, p.x, p.y - y, x, y);
        line(dc, p.x + x, p.y, -x, y);
        line(dc, p.x, p.y + y, -x, -y);
        line(dc, p.x - x, p.y, x, -y);
    } else if (interaction == game::Interaction::unknown) {
        const auto x = std::min<LONG>(4, radius_x), y = std::min<LONG>(4, radius_y);
        Ellipse(dc, p.x - x, p.y - y, p.x + x + 1, p.y + y + 1);
    } else {
        const auto dx = std::min<LONG>(5, r.right - r.left),
                   dy = std::min<LONG>(5, r.bottom - r.top);
        for (const auto x : {r.left, r.right - 1}) {
            for (const auto y : {r.top, r.bottom - 1}) {
                line(dc, x, y, x == r.left ? dx : -dx, 0);
                line(dc, x, y, 0, y == r.top ? dy : -dy);
            }
        }
    }
}

void legend(HDC dc, std::span<const Marker> items, RECT viewport, std::span<const RECT> captions) {
    const auto font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH, L"Segoe UI");
    const auto previous = font ? SelectObject(dc, font) : nullptr;

    struct Label {
        game::Interaction interaction;
        const wchar_t* text;
        SIZE size;
    };

    std::vector<Label> labels;
    LONG width = 0;
    for (const auto& [kind, text] : {std::pair{game::Interaction::click, L"Interact"},
                                     std::pair{game::Interaction::item, L"Target"},
                                     std::pair{game::Interaction::unknown, L"Hotspot"}}) {
        if (std::any_of(items.begin(), items.end(), [&](const Marker& item) {
                return !item.direction && item.interaction == kind;
            })) {
            Label label{kind, ui::translate(text), {}};
            GetTextExtentPoint32W(dc, label.text, static_cast<int>(std::wcslen(label.text)),
                                  &label.size);
            width += 22 + label.size.cx + 12;
            labels.push_back(label);
        }
    }
    if (!labels.empty()) {
        LONG x = std::max<LONG>(viewport.left, (viewport.left + viewport.right - width) / 2);
        LONG y = viewport.bottom <= 380 ? viewport.bottom + 5 : viewport.top - 20;
        bool fits = true;
        if (y < 0) {
            fits = false;
            y = viewport.top + 4;
            for (; y + 16 <= viewport.bottom; y += 18) {
                RECT candidate{x - 4, y - 1, std::min<LONG>(640, x + width), y + 16};
                if (std::none_of(items.begin(), items.end(),
                                 [&](const Marker& item) {
                                     RECT intersection{};
                                     return IntersectRect(&intersection, &candidate, &item.bounds);
                                 }) &&
                    std::none_of(captions.begin(), captions.end(), [&](const RECT& caption) {
                        RECT intersection{};
                        return IntersectRect(&intersection, &candidate, &caption);
                    })) {
                    fits = true;
                    break;
                }
            }
        }
        RECT panel{x - 4, y - 1, std::min<LONG>(640, x + width), y + 16};
        if (!fits || y + 16 > 480) {
            if (previous) {
                SelectObject(dc, previous);
            }
            if (font) {
                DeleteObject(font);
            }
            return;
        }
        FillRect(dc, &panel, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(190, 187, 166));
        for (const auto& label : labels) {
            symbol(dc, {x + 2, y + 1, x + 16, y + 15}, label.interaction);
            RECT text{x + 22, y, std::min<LONG>(640, x + 22 + label.size.cx), y + 16};
            DrawTextW(dc, label.text, -1, &text, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            x += 22 + label.size.cx + 12;
        }
    }
    if (previous) {
        SelectObject(dc, previous);
    }
    if (font) {
        DeleteObject(font);
    }
}

std::vector<RECT> captions(HDC dc, std::span<const Marker> items, RECT viewport) {
    const auto font = CreateFontW(-10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH, L"Segoe UI");
    const auto previous = font ? SelectObject(dc, font) : nullptr;
    std::vector<RECT> placed;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(190, 187, 166));
    for (const auto& item : items) {
        const auto label = item.interaction == game::Interaction::view   ? L"View"
                           : item.interaction == game::Interaction::talk ? L"Talk"
                           : item.interaction == game::Interaction::use  ? L"Use"
                                                                         : nullptr;
        if (!label || item.direction) {
            continue;
        }
        const auto text = ui::translate(label);
        SIZE size{};
        GetTextExtentPoint32W(dc, text, static_cast<int>(std::wcslen(text)), &size);
        const auto width = size.cx + 8, height = size.cy + 2;
        if (width > viewport.right - viewport.left) {
            continue;
        }
        const auto x =
            std::clamp(center(item.bounds).x - width / 2, viewport.left, viewport.right - width);
        for (const auto y : {item.bounds.bottom + 2, item.bounds.top - height - 2}) {
            const RECT panel{x, y, x + width, y + height};
            const auto overlaps = [&](const RECT& bounds) {
                RECT intersection{};
                return IntersectRect(&intersection, &bounds, &panel) != FALSE;
            };
            if (y < viewport.top || panel.bottom > viewport.bottom ||
                std::any_of(items.begin(), items.end(),
                            [&](const Marker& other) { return overlaps(other.bounds); }) ||
                std::any_of(placed.begin(), placed.end(), overlaps)) {
                continue;
            }
            FillRect(dc, &panel, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            RECT bounds{x + 4, y + 1, panel.right - 4, panel.bottom - 1};
            DrawTextW(dc, text, -1, &bounds, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            placed.push_back(panel);
            break;
        }
    }
    if (previous) {
        SelectObject(dc, previous);
    }
    if (font) {
        DeleteObject(font);
    }
    return placed;
}
}

std::vector<Marker> markers(std::span<const game::WorldTarget> targets, RECT viewport,
                            bool exits_only) {
    std::vector<Marker> result;
    const auto area = (viewport.right - viewport.left) * (viewport.bottom - viewport.top);
    if (area <= 0 || viewport.right - viewport.left < 16 || viewport.bottom - viewport.top < 16) {
        return result;
    }
    for (const auto& target : targets) {
        const auto interaction = target.interaction == game::Interaction::view ||
                                 target.interaction == game::Interaction::talk ||
                                 target.interaction == game::Interaction::use ||
                                 target.interaction == game::Interaction::item;
        if (exits_only && (!target.navigation || interaction)) {
            continue;
        }
        const auto& bounds = target.bounds;
        const auto width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0 || (!target.navigation && width * height >= area / 2)) {
            continue;
        }
        Marker marker{target.exposed, game::movement_direction(target.interaction),
                      target.interaction};
        if (marker.direction ||
            (target.navigation && !interaction && (width > 100 || height > 100))) {
            auto point = center(target.exposed);
            const auto x = point.x - viewport.left, y = point.y - viewport.top;
            const auto right = viewport.right - point.x, bottom = viewport.bottom - point.y;
            const auto nearest = std::min({x, y, right, bottom});
            if (!marker.direction) {
                marker.direction = nearest == x ? 1 : nearest == right ? 2 : nearest == y ? 3 : 4;
            }
            const auto margin_x =
                std::min<LONG>(8, (target.exposed.right - target.exposed.left) / 2);
            const auto margin_y =
                std::min<LONG>(8, (target.exposed.bottom - target.exposed.top) / 2);
            if (marker.direction == 1) {
                point.x = target.exposed.left + margin_x;
            } else if (marker.direction == 2) {
                point.x = target.exposed.right - margin_x;
            } else if (marker.direction == 3) {
                point.y = target.exposed.top + margin_y;
            } else {
                point.y = target.exposed.bottom - margin_y;
            }
            const auto radius_x =
                std::min<LONG>(6, (target.exposed.right - target.exposed.left - 1) / 2);
            const auto radius_y =
                std::min<LONG>(6, (target.exposed.bottom - target.exposed.top - 1) / 2);
            point.x = std::clamp(point.x, target.exposed.left + radius_x,
                                 target.exposed.right - radius_x - 1);
            point.y = std::clamp(point.y, target.exposed.top + radius_y,
                                 target.exposed.bottom - radius_y - 1);
            marker.bounds = {point.x - radius_x, point.y - radius_y, point.x + radius_x + 1,
                             point.y + radius_y + 1};
        } else {
            const auto point = center(marker.bounds);
            const auto half_width =
                std::min<LONG>(18, (marker.bounds.right - marker.bounds.left) / 2);
            const auto half_height =
                std::min<LONG>(14, (marker.bounds.bottom - marker.bounds.top) / 2);
            marker.bounds = {point.x - half_width, point.y - half_height, point.x + half_width,
                             point.y + half_height};
        }
        if (IsRectEmpty(&marker.bounds)) {
            continue;
        }
        const auto point = center(marker.bounds);
        if (std::any_of(result.begin(), result.end(), [&](const Marker& other) {
                const auto previous = center(other.bounds);
                return std::abs(point.x - previous.x) < 12 && std::abs(point.y - previous.y) < 12;
            })) {
            continue;
        }
        result.push_back(marker);
    }
    return result;
}

void draw(HDC dc, std::span<const Marker> items, RECT viewport, bool show_labels) {
    const auto saved = SaveDC(dc);
    if (!saved) {
        return;
    }
    const auto pen = CreatePen(PS_SOLID, 1, RGB(190, 187, 166));
    if (!pen) {
        RestoreDC(dc, saved);
        return;
    }
    const auto old = SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    for (const auto& item : items) {
        const auto& r = item.bounds;
        const auto p = center(r);
        const auto arrow_x = std::min<LONG>(4, (r.right - r.left - 1) / 2);
        const auto arrow_y = std::min<LONG>(4, (r.bottom - r.top - 1) / 2);
        if (item.direction == 1 || item.direction == 2) {
            const auto sign = item.direction == 1 ? -1 : 1;
            line(dc, p.x + sign * arrow_x, p.y, -sign * arrow_x, -arrow_y);
            line(dc, p.x + sign * arrow_x, p.y, -sign * arrow_x, arrow_y);
        } else if (item.direction) {
            const auto sign = item.direction == 3 ? -1 : 1;
            line(dc, p.x, p.y + sign * arrow_y, -arrow_x, -sign * arrow_y);
            line(dc, p.x, p.y + sign * arrow_y, arrow_x, -sign * arrow_y);
        } else {
            symbol(dc, r, item.interaction);
        }
    }
    const auto labels = show_labels ? captions(dc, items, viewport) : std::vector<RECT>{};
    legend(dc, items, viewport, labels);
    SelectObject(dc, old);
    RestoreDC(dc, saved);
    DeleteObject(pen);
}

bool Hold::update(bool held, bool available) {
    if (!held) {
        blocked_ = false;
        return false;
    }
    return available && !blocked_;
}

void Hold::suspend() {
    blocked_ = true;
}
}
