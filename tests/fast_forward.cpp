#include "playback/fast_forward.h"
#include "dispatch.h"
#include "settings.h"

#include <fstream>
#include <iostream>

namespace {
void verify_settings() {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"xfiles-speed-" + std::to_wstring(GetCurrentProcessId()) + L".ini");

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};

    std::ofstream(path) << "[Video]\n";
    const auto defaults = read_settings(path);
    test::require(defaults.movie_speed == 2 && defaults.movie_speed_mute,
                  "Held speed must default to muted 2x.");
    test::require(defaults.dialogue_transcript, "Dialogue transcript default was not loaded.");
    test::require(defaults.quick_menu, "Quick menu default was not loaded.");
    test::require(defaults.quick_menu_items == std::array{true, true, true, true, true},
                  "Quick menu buttons must default to visible.");
    for (const auto enabled : {false, true}) {
        std::ofstream(path) << "[Enhancements]\nQuickMenu=" << enabled
                            << "\nSaveBrowser=1\nDialogueTranscript=1\n";
        const auto options = read_settings(path);
        test::require(options.quick_menu == enabled && options.save_browser &&
                          options.dialogue_transcript,
                      "Quick menu setting changed independent enhancements.");
    }
    constexpr std::array item_keys{"Save", "Load", "Transcript", "Tweaks", "Menu"};
    for (std::size_t index = 0; index < item_keys.size(); ++index) {
        std::ofstream(path) << "[Enhancements]\nSaveBrowser=1\nDialogueTranscript=1\n"
                               "[QuickMenu]\n"
                            << item_keys[index] << "=0\n";
        const auto options = read_settings(path);
        auto expected = defaults.quick_menu_items;
        expected[index] = false;
        test::require(options.quick_menu && options.save_browser && options.dialogue_transcript &&
                          options.quick_menu_items == expected,
                      "A hidden button changed another button or enhancement.");
    }
    std::ofstream(path) << "[Enhancements]\nDialogueTranscript=0\n";
    test::require(!read_settings(path).dialogue_transcript,
                  "Dialogue transcript cannot be disabled.");
    std::ofstream(path) << "[Enhancements]\nDialogueTranscript=1\n";
    test::require(read_settings(path).dialogue_transcript,
                  "Dialogue transcript cannot be enabled.");
    for (unsigned speed = 2; speed <= 4; ++speed) {
        std::ofstream(path) << "[Video]\nMovieSpeed=" << speed
                            << "\n[Audio]\nMovieSpeedMute=0\n[Input]\nMovieSpeedKey=0\n";
        const auto options = read_settings(path);
        test::require(options.movie_speed == speed && !options.movie_speed_mute &&
                          options.movie_speed_key == 0 && options.gamepad,
                      "Speed, audible mode or independent keyboard disable was not loaded.");
    }
    for (const auto invalid : {0, 1, 5, -1}) {
        std::ofstream(path) << "[Video]\nMovieSpeed=" << invalid << "\n";
        test::require(read_settings(path).movie_speed == 2,
                      "Invalid speed did not fall back to 2x.");
    }
}

void verify_clip_continuity() {
    playback::FastForwardInput input;
    playback::HeldFastForward first(input), next(input), background(input);
    test::require(!first.update(true, true), "A new session inherited held keyboard input.");
    first.update(false, true);
    test::require(first.update(true, true), "The first movie did not accept the press.");
    first.clear();
    background.clear();
    test::require(!first.active() && next.update(true, true),
                  "A continuous keyboard hold did not reach the next eligible movie.");
    next.reset();
    test::require(!first.update(true, true) && !next.update(true, true),
                  "Pause or seek failed to disarm the shared input.");
    next.update(false, true);
    next.update(true, true);
    first.context(1);
    test::require(!next.active() && next.previously_active() && !next.update(true, true),
                  "A focus barrier did not invalidate another movie's active hold.");
    first.controller(0, true, false);
    first.update(false, true);
    test::require(first.update(first.controller(0, true, true), true),
                  "The first movie did not accept a fresh L3 press.");
    first.clear();
    test::require(next.update(next.controller(0, true, true), true),
                  "A continuous L3 hold did not reach the next eligible movie.");
    next.controller(0, false, false);
    test::require(!first.update(first.controller(0, true, true), true),
                  "A controller reconnect between movies inherited the old hold.");
    test::require(!next.controller(1, true, true),
                  "A second controller inherited the first controller's arming.");
    next.reset();
    test::require(!first.controller(0, true, true),
                  "Pause did not require controller release across movies.");
}
}

int main() {
    try {
        verify_settings();
        verify_clip_continuity();
        playback::HeldFastForward state;
        test::require(!state.update(true, true), "Held input accelerated a new movie.");
        test::require(!state.update(false, true) && state.update(true, true),
                      "A fresh press did not accelerate.");
        test::require(state.update(true, true), "Holding the key lost acceleration.");
        test::require(!state.update(false, true), "Release did not restore normal speed.");
        test::require(state.update(true, true), "Second press did not accelerate.");
        test::require(!state.update(true, false) && !state.update(true, true),
                      "Focus or modal transition retained held acceleration.");
        state.update(false, true);
        state.update(true, true);
        state.reset();
        test::require(!state.active() && !state.update(true, true),
                      "Pause or seek did not require a fresh key press.");
        state.update(false, true);
        state.update(true, true);
        state.context(1);
        test::require(!state.active() && !state.update(true, true),
                      "An unobserved focus transition retained acceleration.");
        state.reset();
        test::require(!state.controller(0, true, true), "New controller inherited a held press.");
        test::require(!state.controller(0, true, false) && state.controller(0, true, true),
                      "Released controller did not accept a fresh press.");
        test::require(!state.controller(1, true, true),
                      "Second controller inherited a held press.");
        test::require(state.controller(0, true, true), "Second controller interrupted the hold.");
        test::require(!state.controller(0, false, false) && !state.controller(0, true, true),
                      "Reconnected controller inherited a held press.");
        state.controller(0, true, false);
        state.context(2);
        test::require(!state.controller(0, true, true),
                      "Context reset retained controller arming.");
        std::cout << "Held movie speed release and context barriers passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
