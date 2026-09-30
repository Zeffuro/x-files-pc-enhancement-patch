#include "enhancements/scene_overlay.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace enhancements;
game::Application app{}, replacement{};
game::Application* current = &app;
void* native_pending = nullptr;
int state = 1, other_state = 2;
int view = 1;
bool eligible = true, load_pending = false, attached = true, modal = false;
unsigned input = 0, pauses = 0, resumes = 0;

void require(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

void __stdcall native_pause(void* owner, int paused, void* queue) {
    require(owner == current->state && queue == reinterpret_cast<std::byte*>(current) + 0x268,
            "Native pause used the wrong scene or queue");
    paused ? ++pauses : ++resumes;
}

std::byte* image() {
    return reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
}
}

void trace_value(const char*, std::uint32_t) {}

namespace enhancements {
bool checkpoint_available() {
    return eligible;
}

bool checkpoint_load_pending() {
    return load_pending;
}
}

namespace enhancements::game {
const Edition& edition() {
    static const Edition result = [] {
        Edition value{};
        value.application =
            static_cast<std::uint32_t>(reinterpret_cast<std::byte*>(&current) - image());
        value.pending_load =
            static_cast<std::uint32_t>(reinterpret_cast<std::byte*>(&native_pending) - image());
        value.pause =
            static_cast<std::uint32_t>(reinterpret_cast<std::byte*>(&native_pause) - image());
        value.main_menu = 123;
        value.movie = 124;
        value.action_movie = 125;
        return value;
    }();
    return result;
}

std::byte* executable_image() {
    return attached ? image() : nullptr;
}

std::uintptr_t input_vtable() {
    return input;
}

bool menu_confirmation_active() {
    return modal;
}

bool saving_available() {
    return true;
}

ScriptControls script_controls() {
    return {};
}
}

int main() {
    try {
        app.state = &state;
        app.view = reinterpret_cast<game::MainView*>(&view);
        SceneOverlay overlay, nested;
        require(overlay.begin() && pauses == 1 && scene_overlay_checkpoint_available(),
                "Gameplay overlay did not acquire one native pause");
        require(!nested.begin() && pauses == 1, "Nested overlay duplicated pause ownership");
        eligible = false;
        require(overlay.valid() && scene_overlay_checkpoint_available(),
                "Own pause lost original checkpoint eligibility");
        overlay.end();
        overlay.end();
        require(resumes == 1 && !scene_overlay_active(), "Cancel did not resume exactly once");
        require(!overlay.begin(), "Save browser bypassed original eligibility");
        require(overlay.begin(false) && !scene_overlay_checkpoint_available(),
                "Dialogue transcript pause authorized a save/load");
        overlay.end();
        eligible = true;
        require(overlay.begin(), "Cannot pause before state invalidation");
        app.state = &other_state;
        overlay.end();
        require(resumes == 2, "Cleanup resumed a replacement state");
        app.state = &state;
        require(overlay.begin(), "Cannot pause before native load");
        native_pending = &state;
        overlay.end();
        require(resumes == 2, "Cleanup resumed during a native pending load");
        native_pending = nullptr;
        require(overlay.begin(), "Cannot pause before in-place load");
        load_pending = true;
        overlay.end(true);
        load_pending = false;
        require(resumes == 2 && !scene_overlay_active(), "In-place load resumed its old owner");
        require(overlay.begin(), "Cannot pause before application replacement");
        current = &replacement;
        overlay.end();
        require(resumes == 2, "Cleanup resumed a replacement application");
        current = &app;
        input = game::edition().movie;
        require(!overlay.begin(false), "Transcript interrupted an active movie");
        input = 0;
        modal = true;
        require(!overlay.begin(false), "Transcript interrupted a native confirmation");
        modal = false;
        std::cout << "Scene pause ownership and load transfer passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
