#include "saves/browser_state.h"
#include "saves/artwork.h"
#include "dispatch.h"
#include <fstream>
#include <iostream>

namespace {
void check_categories(saves::Browser& state) {
    state.saving = false;
    state.page = saves::slot_pages - 1;
    state.selection = 4;
    for (unsigned i = 0; i < 7; ++i) {
        state.legacy.entries.push_back({state.root / (std::to_wstring(i) + L".x"),
                                        L"Existing " + std::to_wstring(i),
                                        L"date",
                                        {},
                                        0,
                                        true});
    }
    state.quicksaves = {
        {0, L"Quick", L"date", state.root / L"QUICK.x", {}, true, true},
        {0, L"Previous quick", L"date", state.root / L"QUICK.old.x", {}, true, true}};
    for (unsigned i = 0; i < 5; ++i) {
        state.autosaves.push_back({0,
                                   L"Auto " + std::to_wstring(i),
                                   L"date",
                                   state.root / (L"AUTO" + std::to_wstring(i) + L".x"),
                                   {},
                                   true,
                                   i != 4});
    }
    saves::cycle_browser_category(state);
    test::require(state.category == saves::BrowserCategory::existing && state.page == 0 &&
                      state.focus == 8 && saves::browser_page_count(state) == 2 &&
                      state.slots[0].name == L"Existing 0",
                  "Existing category inherited the manual page or lost category focus");
    saves::change_browser_page(state, 1);
    state.selection = 0;
    test::require(state.page == 1 && state.slots[0].name == L"Existing 6" &&
                      !state.slots[1].occupied && !saves::control_enabled(state, 12),
                  "Existing page reused a card or enabled removal");
    saves::cycle_browser_category(state);
    test::require(state.category == saves::BrowserCategory::quicksave && state.page == 0 &&
                      state.slots[0].name == L"Quick" && state.slots[1].name == L"Previous quick" &&
                      !state.slots[2].occupied && !saves::control_enabled(state, 12) &&
                      !saves::control_enabled(state, 9),
                  "Quicksave cards were mixed with existing files or made writable");
    saves::change_browser_page(state, 1);
    test::require(state.page == 0, "Quicksave navigation created another page");
    saves::cycle_browser_category(state);
    state.selection = 4;
    state.focus = 4;
    test::require(state.category == saves::BrowserCategory::autosaves &&
                      state.slots[0].name == L"Auto 0" && state.slots[4].name == L"Auto 4" &&
                      !state.slots[5].occupied && !saves::control_enabled(state, 11) &&
                      !saves::control_enabled(state, 12),
                  "Autosave order, read-only controls or unreadable load handling failed");
    saves::cycle_browser_focus(state, 1);
    test::require(saves::control_enabled(state, state.focus),
                  "Focus moved onto a disabled autosave action");
    state.confirm = true;
    const auto category = state.category;
    saves::cycle_browser_category(state);
    test::require(state.category == category, "Confirmation allowed a category change");
    state.confirm = false;
    saves::cycle_browser_category(state);
    test::require(state.category == saves::BrowserCategory::manual &&
                      state.page == saves::slot_pages - 1 && state.selection == 4 &&
                      state.focus == 8 && state.slots[4].number == 599,
                  "Manual page and selection were not restored after category cycling");
    saves::cycle_browser_category(state);
    test::require(state.page == 1 && state.selection == 0 && state.slots[0].name == L"Existing 6",
                  "Existing category did not restore its own page and selection");
    state.saving = true;
    saves::load_browser_page(state);
    saves::cycle_browser_category(state);
    test::require(state.category == saves::BrowserCategory::manual &&
                      !saves::control_enabled(state, 8) && saves::control_enabled(state, 9),
                  "Saving browser allowed a non-manual category");
    state.slots[state.selection].occupied = true;
    test::require(saves::control_enabled(state, 12) && saves::control_enabled(state, 11),
                  "Manual save controls lost overwrite or removal access");
    state.page = state.selection = 0;
    state.focus = 0;
    state.legacy = {};
    state.quicksaves.clear();
    state.autosaves.clear();
    state.positions = {};
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        saves::Browser state;
        state.saving = true;
        state.focus = 3;
        saves::move_browser_focus(state, 0, 1);
        test::require(state.focus == 6, "D-pad cannot leave the slot grid");
        state.focus = 9;
        saves::move_browser_focus(state, 0, 1);
        test::require(state.focus == 10 || state.focus == 11,
                      "D-pad cannot reach the footer from the name field");
        state.confirm = true;
        state.focus = 10;
        saves::move_browser_focus(state, 1, 0);
        test::require(state.focus == 11, "Confirmation buttons cannot be navigated");
        state.confirm = false;
        state.focus = 0;
        test::require(saves::valid_scene_reference({L"XN/39044.xmv", 1, 0, true}) &&
                          saves::valid_scene_reference({L"navm.nmv", 1, 23, false}) &&
                          !saves::valid_scene_reference({L"navm.nmv", 1, 23, true}) &&
                          !saves::valid_scene_reference({L"../XN/39044.xmv", 1, 0, true}) &&
                          !saves::valid_scene_reference({L"XV/39044.xmv", 1, 0, true}),
                      "Scene preview reference validation failed");
        state.root = argc > 1 ? std::filesystem::path(argv[1])
                              : std::filesystem::temp_directory_path() /
                                    (L"xfiles-browser-" + std::to_wstring(GetCurrentProcessId()));
        if (argc == 1) {
            check_categories(state);
        }
        const RECT bounds{0, 0, 640, 480};
        FillRect(state.background.dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (argc > 1) {
            test::require(
                saves::draw_artwork(state.background.dc, state.root, "savegameMAIN.pic", bounds),
                "Installed save artwork could not be decoded");
        }
        state.saving = argc < 4 || std::wstring_view(argv[3]) != L"load";
        state.text = saves::load_browser_text(state.root);
        if (!state.saving) {
            test::require(saves::draw_artwork(state.background.dc, state.root, "loadTOPpatch.pic",
                                              {253, 20, 387, 78}),
                          "Installed load title could not be decoded");
        }
        saves::load_browser_page(state);
        saves::load_browser_art(state);
        if (argc > 4 && std::wstring_view(argv[4]) == L"sample") {
            const auto movie = media::Movie::open(state.root / "XN" / "39044.xmv");
            const media::Track* video = nullptr;
            for (const auto& track : movie.tracks) {
                if (track.handler == "vide" && !track.samples.empty()) {
                    video = &track;
                    break;
                }
            }
            test::require(video != nullptr, "No scene video track");
            saves::ScenePreview preview(state.root, {L"XN/39044.xmv", video->id, 0, true});
            const auto first = preview.update(0);
            const auto later = preview.update(1500);
            test::require(first.width && later.width && !later.pixels.empty(),
                          "Scene preview did not decode video");
            test::require(first.pixels != later.pixels, "Motion preview remained on one frame");
            const auto saved_time = 1500ull * movie.timescale / 1000;
            const auto saved_sample = video->sample_at(saved_time, movie.timescale);
            test::require(saved_sample.has_value(), "Saved frame has no timeline position");
            saves::ScenePreview resumed(
                state.root, {L"XN/39044.xmv", video->id, *saved_sample, true, saved_time});
            test::require(resumed.update(0).pixels == later.pixels,
                          "Hover preview jumped away from the saved movie position");
            saves::ScenePreview still(state.root, {L"XN/39044.xmv", video->id, 0, false});
            const auto still_first = still.update(0);
            test::require(still_first.pixels == still.update(1500).pixels,
                          "Static scene preview advanced to another image");
            for (unsigned i = 0; i < saves::slots_per_page; ++i) {
                state.thumbnails[i] = {later.width, later.height, later.pixels};
                state.slots[i].occupied = state.slots[i].readable = true;
                state.slots[i].date = L"2026-09-27 08:41";
            }
        }
        if (argc == 1) {
            std::filesystem::create_directories(state.root);
            const auto save = state.root / "scene.x";
            const saves::SceneReference reference{L"navm.nmv", 1, 23, false};
            saves::write_scene_reference(save, reference);
            const auto restored = saves::read_scene_reference(save);
            test::require(restored.movie == reference.movie && restored.sample == 23 &&
                              restored.track == 1 && !restored.motion,
                          "Scene reference did not survive serialization");
            std::ofstream(state.root / "bad.preview") << "XFSCENE1\n../39044.xmv\n1 0 1\n";
            test::require(!saves::read_scene_reference(state.root / "bad.x").track,
                          "Unsafe preview path was accepted from disk");
        }
        saves::draw_browser(state);
        const auto capture = saves::capture_scene(state.output.dc, bounds);
        test::require(capture.width == 300 && capture.height == 225 &&
                          capture.pixels.size() == 300 * 225 * 4,
                      "Save browser capture has invalid dimensions");
        if (argc > 2) {
            BITMAPFILEHEADER header{};
            header.bfType = 0x4d42;
            header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
            header.bfSize = header.bfOffBits + 640 * 480 * 4;
            BITMAPINFOHEADER info{};
            info.biSize = sizeof(info);
            info.biWidth = 640;
            info.biHeight = -480;
            info.biPlanes = 1;
            info.biBitCount = 32;
            std::ofstream output(std::filesystem::path(argv[2]), std::ios::binary);
            output.write(reinterpret_cast<const char*>(&header), sizeof(header));
            output.write(reinterpret_cast<const char*>(&info), sizeof(info));
            output.write(reinterpret_cast<const char*>(state.output.pixels), 640 * 480 * 4);
        }
        std::cout << "Save browser paints and captures without a window.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
