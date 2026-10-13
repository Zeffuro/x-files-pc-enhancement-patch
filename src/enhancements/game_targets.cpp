#include "game_ui.h"
#include "focus.h"
#include "world_interactions.h"
#include "world_cursors.h"
#include "game/layouts/native_objects.h"
#include "game/layouts/database/story.h"
#include "game/functions/runtime.h"
#include "game/access/ui.h"
#include "game/access/world.h"
#include "game/access/database.h"

#include <algorithm>
#include <memory>

namespace enhancements::game {
namespace {

bool enabled(const std::byte* object) {
    const native_game::ObjectFlagsRef flags(object);
    return object && *flags.enabled().get() && !*flags.blocked().get();
}

Application* application() {
    const auto image = executable_image();
    return image ? *native_game::NativeRuntime(image, edition()).application().get() : nullptr;
}

template <typename Callback>
void visit(native_game::MemoryRef<List<void>> group, Callback callback) {
    const auto& list = *group.get();
    if (list.count > 256) {
        return;
    }
    auto node = list.first;
    for (unsigned i = 0; node && i < list.count; ++i, node = node->next) {
        const auto object = static_cast<std::byte*>(node->value);
        if (enabled(object)) {
            callback(object);
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
    const native_game::SceneStateRef state(app->state);
    for (const auto group : {state.navigation(), state.pictures(), state.hotspots()}) {
        visit(group, [&](std::byte*) { found = true; });
    }
    return found;
}

RECT scene_bounds() {
    const auto image = executable_image();
    return image ? native_game::NativeRuntime(image, edition()).viewport().get()->bounds : RECT{};
}

std::vector<WorldTarget> world_targets(bool include_occluded) {
    std::vector<WorldTarget> result;
    if (!world_navigation_available()) {
        return result;
    }
    const auto image = executable_image();
    const native_game::NativeRuntime native(image, edition());
    const auto app = application();
    const native_game::SceneStateRef state(app->state);
    std::vector<RECT> occluders;
    const auto add = [&](const RECT& bounds, bool navigation, const void* identity,
                         Interaction kind) {
        navigation = navigation || movement_direction(kind) != 0;
        RECT clipped{};
        const auto& viewport = native.viewport().get()->bounds;
        if (IntersectRect(&clipped, &bounds, &viewport) && clipped.left >= 0 && clipped.top >= 0 &&
            clipped.right <= 640 && clipped.bottom <= 480) {
            RECT exposed{};
            if (include_occluded || exposed_target(clipped, occluders, exposed)) {
                result.push_back({clipped, include_occluded ? clipped : exposed,
                                  reinterpret_cast<std::uintptr_t>(identity), navigation, kind});
            }
            occluders.push_back(clipped);
        }
    };
    // Match native hit-test priority so a picture cannot intercept a jump to an exit.
    visit(state.pictures(), [&](std::byte* object) {
        const auto resource = native.object_resource(object);
        if (resource) {
            add(native_game::PictureResourceRef(resource).rectangle().get()->bounds, false, object,
                world_interaction(object, resource, app, image, edition()));
        }
    });
    visit(state.hotspots(), [&](std::byte* object) {
        const auto resource = *native_game::ResourceObjectRef(object).resource().get();
        if (resource) {
            add(native_game::StoryHotspotRef(resource).rectangle().get()->bounds, false, object,
                world_interaction(object, resource, app, image, edition()));
        }
    });
    visit(state.navigation(), [&](std::byte* object) {
        const auto resource =
            const_cast<void*>(*native_game::ResourceObjectRef(object).resource().get());
        if (!resource) {
            return;
        }
        // The native lookup returns an owned resource reference, even on cache hits.
        const std::unique_ptr<std::byte, native_game::NativeFunctions::Release> shape(
            native.lookup(resource, nullptr), native.release_target());
        if (shape) {
            add(native_game::StoryHotspotRef(shape.get()).rectangle().get()->bounds, true, object,
                navigation_cursor_interaction(object, resource, shape.get(), image, edition()));
        }
    });
    return result;
}

std::vector<RECT> world_hotspots(bool navigation_only, bool include_occluded) {
    std::vector<RECT> result;
    for (const auto& target : world_targets(include_occluded)) {
        if (!navigation_only || target.navigation) {
            result.push_back(target.exposed);
        }
    }
    std::sort(result.begin(), result.end(), [](const RECT& a, const RECT& b) {
        const auto ax = a.left + a.right, bx = b.left + b.right;
        return ax == bx ? a.top + a.bottom < b.top + b.bottom : ax < bx;
    });
    result.erase(std::unique(result.begin(), result.end(),
                             [include_occluded](const RECT& a, const RECT& b) {
                                 if (include_occluded) {
                                     return EqualRect(&a, &b) != FALSE;
                                 }
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
           *native_game::PreferencesRef(app->preferences).skip_movies().get() &&
           *native_game::MovieInputRef(current_input()).skippable().get();
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
        native_game::NativeRuntime(executable_image(), edition()).viewport().get()->bounds;
    // Action hit areas move with the movie. Read the current native rectangles.
    visit(native_game::SceneStateRef(app->state).aiming_targets(), [&](std::byte* object) {
        const auto& compact = *native_game::AimingTargetRef(object).bounds().get();
        RECT bounds{compact[0], compact[1], compact[2], compact[3]};
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
    visit(native_game::SceneStateRef(app->state).emotions(), [&](std::byte* object) {
        if (*native_game::MemoryRef<void*>(object).get() !=
            native_game::NativeRuntime(executable_image(), edition()).emotion_type()) {
            return;
        }
        const auto bounds = *native_game::EmotionRef(object).rectangle().bounds().get();
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
    const native_game::MainViewRef view(app->view);
    const auto background = view.modal_background().get();
    const auto& foreground = *view.modal_foreground().get();
    if (!background->count || !foreground.count || foreground.count > 4) {
        return result;
    }
    // Native modal groups contain a text object followed by up to three button pictures.
    auto node = foreground.first;
    for (unsigned i = 0; node && i < foreground.count; ++i, node = node->next) {
        const auto entry = node->value;
        if (!entry || !entry->icon ||
            *native_game::MemoryRef<void*>(entry->icon).get() !=
                native_game::NativeRuntime(executable_image(), edition()).picture_type()) {
            continue;
        }
        Rectangle rectangle{};
        native_game::NativeFunctions(executable_image(), edition())
            .picture_bounds(entry->icon, &rectangle);
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
