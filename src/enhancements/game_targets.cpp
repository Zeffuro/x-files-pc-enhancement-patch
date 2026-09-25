#include "game_ui.h"
#include "focus.h"

#include <algorithm>
#include <memory>

namespace enhancements::game {
namespace {

bool enabled(const std::byte* object) {
    return object && *reinterpret_cast<const unsigned*>(object + 8) &&
           !*reinterpret_cast<const unsigned*>(object + 12);
}

Application* application() {
    const auto image = executable_image();
    return image ? *reinterpret_cast<Application**>(image + edition().application) : nullptr;
}

template <typename Callback> void visit(void* state, std::size_t offset, Callback callback) {
    const auto& list = *reinterpret_cast<List<std::byte>*>(static_cast<std::byte*>(state) + offset);
    if (list.count > 256) {
        return;
    }
    auto node = list.first;
    for (unsigned i = 0; node && i < list.count; ++i, node = node->next) {
        if (enabled(node->value)) {
            callback(node->value);
        }
    }
}

}

bool world_navigation_available() {
    const auto app = application();
    if (!app || !app->state || current_input()) {
        return false;
    }
    bool found = false;
    for (const auto offset : {0x68, 0x7c, 0x194}) {
        visit(app->state, offset, [&](std::byte*) { found = true; });
    }
    return found;
}

RECT scene_bounds() {
    const auto image = executable_image();
    return image ? reinterpret_cast<Rectangle*>(image + edition().viewport)->bounds : RECT{};
}

std::vector<RECT> world_hotspots(bool navigation_only) {
    std::vector<RECT> result;
    if (!world_navigation_available()) {
        return result;
    }
    const auto image = executable_image();
    const auto app = application();
    std::vector<RECT> occluders;
    const auto add = [&](const RECT& bounds, bool navigation) {
        RECT clipped{};
        const auto& viewport = reinterpret_cast<Rectangle*>(image + edition().viewport)->bounds;
        if (IntersectRect(&clipped, &bounds, &viewport) && clipped.left >= 0 && clipped.top >= 0 &&
            clipped.right <= 640 && clipped.bottom <= 480) {
            RECT exposed{};
            if ((!navigation_only || navigation) && exposed_target(clipped, occluders, exposed)) {
                result.push_back(exposed);
            }
            occluders.push_back(clipped);
        }
    };
    // Match native hit-test priority so a picture cannot intercept a jump to an exit.
    visit(app->state, 0x7c, [&](std::byte* object) {
        using Resource = std::byte*(__stdcall*)(void*);
        const auto methods = *reinterpret_cast<void***>(object);
        const auto resource = reinterpret_cast<Resource>(methods[1])(object);
        if (resource) {
            add(reinterpret_cast<Rectangle*>(resource + 0x30)->bounds, false);
        }
    });
    visit(app->state, 0x194, [&](std::byte* object) {
        const auto resource = *reinterpret_cast<std::byte**>(object + 0x18);
        if (resource) {
            add(reinterpret_cast<Rectangle*>(resource + 0x2c)->bounds, false);
        }
    });
    visit(app->state, 0x68, [&](std::byte* object) {
        using Lookup = std::byte*(__stdcall*)(void*, void*);
        using Release = void(__thiscall*)(void*);
        const auto resource = *reinterpret_cast<void**>(object + 0x18);
        if (!resource) {
            return;
        }
        // The native lookup returns an owned resource reference, even on cache hits.
        const std::unique_ptr<std::byte, Release> shape(
            reinterpret_cast<Lookup>(image + edition().lookup)(resource, nullptr),
            reinterpret_cast<Release>(image + edition().release));
        if (shape) {
            add(reinterpret_cast<Rectangle*>(shape.get() + 0x2c)->bounds, true);
        }
    });
    std::sort(result.begin(), result.end(), [](const RECT& a, const RECT& b) {
        const auto ax = a.left + a.right, bx = b.left + b.right;
        return ax == bx ? a.top + a.bottom < b.top + b.bottom : ax < bx;
    });
    result.erase(std::unique(result.begin(), result.end(),
                             [](const RECT& a, const RECT& b) {
                                 return a.left + a.right == b.left + b.right &&
                                        a.top + a.bottom == b.top + b.bottom;
                             }),
                 result.end());
    return result;
}

bool movie_skippable() {
    const auto input = input_vtable();
    if (input != edition().movie && input != edition().action_movie) {
        return false;
    }
    const auto app = application();
    return app && app->preferences &&
           *reinterpret_cast<unsigned*>(static_cast<std::byte*>(app->preferences) + 0x74) &&
           *reinterpret_cast<unsigned*>(static_cast<std::byte*>(current_input()) + 0x2c);
}

std::vector<RECT> aiming_targets() {
    std::vector<RECT> result;
    const auto app = application();
    const auto input = input_vtable();
    if (!app || !app->state ||
        (input && input != edition().movie && input != edition().action_movie)) {
        return result;
    }
    const auto& viewport =
        reinterpret_cast<Rectangle*>(executable_image() + edition().viewport)->bounds;
    // Action hit areas move with the movie; use the current native rectangles each time.
    visit(app->state, 0x248, [&](std::byte* object) {
        const auto compact = reinterpret_cast<const std::uint16_t*>(object + 0x18);
        RECT bounds{compact[2], compact[3], compact[4], compact[5]};
        OffsetRect(&bounds, viewport.left, viewport.top);
        RECT clipped{};
        if (IntersectRect(&clipped, &bounds, &viewport) && clipped.left >= 0 && clipped.top >= 0 &&
            clipped.right <= 640 && clipped.bottom <= 480) {
            result.push_back(clipped);
        }
    });
    return result;
}

std::vector<RECT> emotion_targets() {
    std::vector<RECT> result;
    const auto app = application();
    if (!app || !app->state || current_input()) {
        return result;
    }
    visit(app->state, 0x130, [&](std::byte* object) {
        if (*reinterpret_cast<void**>(object) != executable_image() + edition().emotion) {
            return;
        }
        const auto bounds = reinterpret_cast<Rectangle*>(object + 0x1cc)->bounds;
        if (bounds.left >= 0 && bounds.top >= 0 && bounds.right <= 640 && bounds.bottom <= 480 &&
            bounds.right > bounds.left && bounds.bottom > bounds.top) {
            result.push_back(bounds);
        }
    });
    return result;
}

std::vector<RECT> modal_buttons() {
    std::vector<RECT> result;
    const auto app = application();
    if (!app || !app->view) {
        return result;
    }
    const auto view = reinterpret_cast<std::byte*>(app->view);
    const auto background = reinterpret_cast<Container*>(view + 0x59c);
    const auto& foreground = *reinterpret_cast<List<InventoryItem>*>(view + 0x5d8);
    if (!background->count || !foreground.count || foreground.count > 4) {
        return result;
    }
    // Native modal groups contain a text object followed by up to three button pictures.
    auto node = foreground.first;
    for (unsigned i = 0; node && i < foreground.count; ++i, node = node->next) {
        const auto entry = node->value;
        if (!entry || !entry->icon ||
            *reinterpret_cast<void**>(entry->icon) != executable_image() + edition().picture) {
            continue;
        }
        using Bounds = Rectangle*(__stdcall*)(void*, Rectangle*);
        Rectangle rectangle{};
        reinterpret_cast<Bounds>(executable_image() + edition().picture_bounds)(entry->icon,
                                                                                &rectangle);
        auto bounds = rectangle.bounds;
        OffsetRect(&bounds, entry->position.x, entry->position.y);
        if (bounds.left >= 0 && bounds.top >= 0 && bounds.right <= 640 && bounds.bottom <= 480 &&
            bounds.right > bounds.left && bounds.bottom > bounds.top) {
            result.push_back(bounds);
        }
    }
    return result;
}

}
