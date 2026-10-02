#include "game_state.h"
#include "game/render/native_render.h"
#include "game/render/canvas_surface.h"
#include <memory>

namespace devtools {
namespace {
HWND owner = nullptr;
std::vector<RECT> rectangles;
std::unique_ptr<native_game::CanvasSurface> surface;

HDC paint(HDC background) {
    if (rectangles.empty() || !surface || !IsWindow(owner) || IsIconic(owner)) {
        return background;
    }
    const auto dc = surface->copy(background);
    if (!dc) {
        return background;
    }
    const auto accent = CreateSolidBrush(RGB(40, 220, 180));
    for (auto target : rectangles) {
        FrameRect(dc, &target, accent);
        InflateRect(&target, -1, -1);
        if (!IsRectEmpty(&target)) {
            FrameRect(dc, &target, accent);
        }
    }
    DeleteObject(accent);
    return dc;
}

bool equal(const std::vector<RECT>& left, const std::vector<RECT>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!EqualRect(&left[index], &right[index])) {
            return false;
        }
    }
    return true;
}
}

void show_hotspots(HWND game, bool enabled, const std::vector<RECT>& targets) {
    const std::vector<RECT> empty;
    const auto& next = enabled && IsWindow(game) ? targets : empty;
    if (game == owner && equal(rectangles, next)) {
        return;
    }
    if (!next.empty() && !surface) {
        try {
            surface = std::make_unique<native_game::CanvasSurface>();
        } catch (...) {
            return;
        }
    }
    owner = game;
    rectangles = next;
    native_game::set_canvas_targets(rectangles.empty() ? nullptr : paint);
    native_game::invalidate_canvas();
    if (IsWindow(game)) {
        InvalidateRect(game, nullptr, FALSE);
    }
}

void release_hotspots() {
    native_game::set_canvas_targets(nullptr);
    if (!rectangles.empty()) {
        native_game::invalidate_canvas();
    }
    owner = nullptr;
    rectangles.clear();
    surface.reset();
}
}
