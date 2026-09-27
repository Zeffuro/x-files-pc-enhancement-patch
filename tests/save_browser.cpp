#include "saves/browser_state.h"
#include "saves/artwork.h"
#include "dispatch.h"
#include <fstream>
#include <iostream>

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
