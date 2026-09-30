#pragma once
#include "game_ui.h"

namespace enhancements {
bool scene_overlay_active();
bool scene_overlay_checkpoint_available();
bool scene_overlay_available(bool exploration_only = true);

class SceneOverlay {
public:
    SceneOverlay() = default;
    SceneOverlay(const SceneOverlay&) = delete;
    SceneOverlay& operator=(const SceneOverlay&) = delete;
    ~SceneOverlay();
    bool begin(bool exploration_only = true);
    bool active() const;
    bool valid() const;
    bool checkpoint_eligible() const;
    void end(bool abandon = false);

private:
    game::Application* application_ = nullptr;
    void* state_ = nullptr;
    game::MainView* view_ = nullptr;
    bool checkpoint_eligible_ = false;
};
}
