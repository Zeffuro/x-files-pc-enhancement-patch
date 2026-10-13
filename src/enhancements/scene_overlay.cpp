#include "game/access/ui.h"
#include "game/functions/runtime.h"
#include "scene_overlay.h"
#include "quick_save.h"
#include "runtime.h"
#include "playback/inspection.h"

namespace enhancements {
namespace {
SceneOverlay* owner = nullptr;

bool scene_movie_playing() {
    const auto bounds = game::scene_bounds();
    return playback::scene_movie_playing(bounds.right - bounds.left, bounds.bottom - bounds.top);
}

game::Application* application() {
    const auto image = game::executable_image();
    return image ? *native_game::NativeRuntime(image, game::edition()).application().get()
                 : nullptr;
}

void pause(game::Application* app, bool paused) {
    native_game::NativeFunctions(game::executable_image(), game::edition())
        .pause(app->state, paused ? 1 : 0, native_game::ApplicationRef(app).commands().data());
}
}

SceneOverlay::~SceneOverlay() {
    end();
}

bool scene_overlay_active() {
    return owner && owner->valid();
}

bool scene_overlay_checkpoint_available() {
    return owner && owner->valid() && owner->checkpoint_eligible();
}

bool scene_overlay_available(bool exploration_only) {
    const auto image = game::executable_image();
    const auto& profile = game::edition();
    if (!image || game::input_vtable() == profile.main_menu || checkpoint_load_pending() ||
        game::menu_confirmation_active() ||
        *native_game::NativeRuntime(image, profile).pending_load().get() || scene_movie_playing()) {
        return false;
    }
    if (exploration_only) {
        return checkpoint_available();
    }
    const auto input = game::input_vtable();
    return game::saving_available() && input != profile.movie && input != profile.action_movie &&
           !game::script_controls().text_input;
}

bool SceneOverlay::begin(bool exploration_only) {
    if (owner || active() || !scene_overlay_available(exploration_only)) {
        return false;
    }
    const auto app = application();
    if (!app || !app->state || !app->view) {
        return false;
    }
    checkpoint_eligible_ = checkpoint_available();
    application_ = app;
    state_ = app->state;
    view_ = app->view;
    owner = this;
    pause(app, true);
    trace_value("scene_overlay_pause", 1);
    return true;
}

bool SceneOverlay::active() const {
    return application_ != nullptr;
}

bool SceneOverlay::owns_scene() const {
    return active() && application() == application_ && application_->state == state_ &&
           application_->view == view_;
}

bool SceneOverlay::valid() const {
    return owns_scene() && !scene_movie_playing();
}

bool SceneOverlay::checkpoint_eligible() const {
    return checkpoint_eligible_;
}

void SceneOverlay::end(bool abandon) {
    // A successful load owns its new scene's resume, including in-place state replacement.
    const auto image = game::executable_image();
    const bool pending =
        checkpoint_load_pending() ||
        (image && *native_game::NativeRuntime(image, game::edition()).pending_load().get());
    if (!abandon && owns_scene() && !pending) {
        pause(application_, false);
        trace_value("scene_overlay_pause", 0);
    }
    application_ = nullptr;
    state_ = nullptr;
    view_ = nullptr;
    checkpoint_eligible_ = false;
    if (owner == this) {
        owner = nullptr;
    }
}
}
