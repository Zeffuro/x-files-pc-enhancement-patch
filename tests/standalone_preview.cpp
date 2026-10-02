#include "devtools/database/standalone_preview.h"
#include "settings.h"
#include <commctrl.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

const Settings& settings() {
    static const Settings defaults;
    return defaults;
}

void trace_value(const char*, std::uint32_t) {}

namespace {
using Data = std::vector<std::uint8_t>;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::wstring text(HWND window) {
    std::wstring value(GetWindowTextLengthW(window) + 1, L'\0');
    value.resize(GetWindowTextW(window, value.data(), static_cast<int>(value.size())));
    return value;
}

Data words(std::initializer_list<std::uint32_t> values) {
    Data result;
    for (const auto value : values) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            result.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }
    return result;
}

Data atom(const char* name, const Data& body) {
    auto result = words({static_cast<std::uint32_t>(body.size() + 8)});
    result.insert(result.end(), name, name + 4);
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

void append(Data& target, const char* name, const Data& body) {
    const auto bytes = atom(name, body);
    target.insert(target.end(), bytes.begin(), bytes.end());
}

Data movie(bool untimed = false, bool multiple = false, bool zero_duration = false,
           bool gap = false) {
    Data description(78);
    description[7] = 1;
    description[25] = description[27] = 4;
    description[75] = 24;
    auto descriptions = words({0, 1});
    append(descriptions, "rpza", description);
    Data table;
    append(table, "stsd", descriptions);
    append(table, "stco", words({0, 1, 8}));
    append(table, "stsc", words({0, 1, 1, 2, 1}));
    append(table, "stsz", words({0, 7, 2}));
    append(table, "stts", words({0, 1, 2, untimed ? 0u : 1000u}));
    append(table, "stss", words({0, 2, 1, 2}));
    auto handler = words({0, 0});
    handler.insert(handler.end(), {'v', 'i', 'd', 'e'});
    Data media;
    append(media, "mdhd", words({0, 0, 0, 1000, 2000}));
    append(media, "hdlr", handler);
    append(media, "minf", atom("stbl", table));
    Data track;
    append(track, "tkhd", words({multiple ? 0u : 15u, 0, 0, 1, 0, 2000}));
    if (gap) {
        append(track, "edts", atom("elst", words({0, 2, 1000, 0xffffffff, 65536, 1000, 0, 65536})));
    }
    append(track, "mdia", media);
    Data header;
    append(header, "mvhd", words({0, 0, 0, 1000, zero_duration ? 0u : 2000u}));
    append(header, "trak", track);
    if (multiple) {
        Data other_table;
        append(other_table, "stsd", descriptions);
        append(other_table, "stco", words({0, 1, 15}));
        append(other_table, "stsc", words({0, 1, 1, 1, 1}));
        append(other_table, "stsz", words({0, 7, 1}));
        append(other_table, "stts", words({0, 1, 1, 1000}));
        append(other_table, "stss", words({0, 1, 1}));
        Data other_media;
        append(other_media, "mdhd", words({0, 0, 0, 1000, 1000}));
        append(other_media, "hdlr", handler);
        append(other_media, "minf", atom("stbl", other_table));
        Data other_track;
        append(other_track, "tkhd", words({15, 0, 0, 2, 0, 1000}));
        append(other_track, "mdia", other_media);
        append(header, "trak", other_track);
    }
    Data result;
    append(result, "mdat", {0xe1, 0, 0, 7, 0xa0, 0x03, 0xe0, 0xe1, 0, 0, 7, 0xa0, 0x7c, 0});
    append(result, "moov", header);
    return result;
}

LRESULT CALLBACK count_text(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR,
                            DWORD_PTR counter) {
    if (message == WM_SETTEXT) {
        ++*reinterpret_cast<unsigned*>(counter);
    }
    return DefSubclassProc(window, message, value, data);
}

void pixels(devtools::standalone::PreviewWindow& state, COLORREF expected) {
    const auto screen = GetDC(nullptr);
    const auto dc = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, 80, 60);
    require(dc && bitmap, "Cannot create capture surface");
    const auto previous = SelectObject(dc, bitmap);
    RECT bounds{0, 0, 80, 60};
    FillRect(dc, &bounds, GetSysColorBrush(COLOR_BTNFACE));
    DRAWITEMSTRUCT item{};
    item.hDC = dc;
    item.hwndItem = state.picture;
    item.rcItem = bounds;
    SendMessageW(state.window, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&item));
    const auto center = GetPixel(dc, 40, 30), edge = GetPixel(dc, 0, 0);
    const auto outside_image = GetPixel(dc, 44, 30);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    if (center != expected) {
        std::cerr << "Center pixel: " << center << ", expected: " << expected << '\n';
    }
    require(center == expected, "Buffered frame pixels missing");
    require(edge == RGB(0, 0, 0), "Letterbox was not painted black");
    if (state.view.actual) {
        require(outside_image == RGB(0, 0, 0), "Actual size enlarged the small frame");
    }
}
}

int main(int argc, char** argv) {
    const bool visible = argc > 1 && std::string(argv[1]) != "--root";
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root =
        std::filesystem::path(temp) / (L"xfiles-preview-" + std::to_wstring(GetCurrentProcessId()));
    devtools::standalone::PreviewWindow state;
    try {
        std::filesystem::create_directories(root);
        const auto data = movie();
        std::ofstream(root / L"test.xmv", std::ios::binary)
            .write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        state.module = GetModuleHandleW(nullptr);
        state.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        state.player = std::make_unique<devtools::Preview>(root, L"test.xmv");
        state.player->captions({{0, 1000, L"First"}, {1000, 2000, L"Second"}});
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES};
        require(InitCommonControlsEx(&controls), "Cannot initialize controls");
        WNDCLASSW type{};
        type.lpfnWndProc = devtools::standalone::preview_proc;
        type.hInstance = state.module;
        type.lpszClassName = L"XFilesPreviewRegression";
        require(RegisterClassW(&type) != 0, "Cannot register preview");
        require(CreateWindowExW(0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                0, 0, 760, 600, nullptr, nullptr, state.module, &state) != nullptr,
                "Cannot create preview");
        KillTimer(state.window, 1);
        require(!state.frames && !state.previous && !state.next,
                "Frame navigation appeared for ordinary movie");
        if (visible) {
            ShowWindow(state.window, SW_SHOW);
            UpdateWindow(state.window);
        }
        pixels(state, RGB(0, 255, 0));
        require(text(state.caption) == L"First", "Initial caption missing");
        ValidateRect(state.picture, nullptr);
        unsigned text_updates = 0;
        for (const auto control : {state.clock, state.caption, state.play}) {
            SetWindowSubclass(control, count_text, 2, reinterpret_cast<DWORD_PTR>(&text_updates));
        }
        for (unsigned tick = 0; tick < 50; ++tick) {
            SendMessageW(state.window, WM_TIMER, 1, 0);
        }
        require(!GetUpdateRect(state.picture, nullptr, FALSE), "Paused timer repainted frame");
        require(text_updates == 0, "Paused timer rewrote unchanged controls");
        require(SendMessageW(state.picture, WM_ERASEBKGND, 0, 0) == 1,
                "Picture did not suppress background erase");
        SendMessageW(state.seek, TBM_SETPOS, FALSE, 600);
        SendMessageW(state.window, WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, 600),
                     reinterpret_cast<LPARAM>(state.seek));
        require(state.painted_sample == 1, "Seek did not select the new frame");
        if (visible) {
            require(GetUpdateRect(state.picture, nullptr, FALSE),
                    "Seek did not invalidate new frame");
        }
        pixels(state, RGB(255, 0, 0));
        require(text(state.caption) == L"Second", "Seek did not update caption");
        require(state.player->time() == 1200 && !state.player->playing(),
                "Paused seek changed playback state");
        SendMessageW(state.window, WM_COMMAND, devtools::standalone::stop_id, 0);
        pixels(state, RGB(0, 255, 0));
        require(text(state.caption) == L"First", "Stop did not restore caption");
        SendMessageW(state.window, WM_COMMAND, devtools::standalone::play_id, 0);
        require(state.player->playing(), "Play failed");
        SendMessageW(state.window, WM_COMMAND, devtools::standalone::play_id, 0);
        require(!state.player->playing(), "Pause failed");
        state.player->seek(state.player->duration());
        devtools::standalone::preview_tick(state);
        pixels(state, RGB(255, 0, 0));
        MoveWindow(state.window, 0, 0, 900, 700, TRUE);
        pixels(state, RGB(255, 0, 0));
        SendMessageW(state.size, CB_SETCURSEL, 1, 0);
        SendMessageW(state.window, WM_COMMAND,
                     MAKEWPARAM(devtools::standalone::size_id, CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(state.size));
        pixels(state, RGB(255, 0, 0));
        DestroyWindow(state.window);
        require(!state.window && !state.player, "Close retained decoder or window");
        std::ofstream(root / L"test.NMV", std::ios::binary)
            .write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        state.player = std::make_unique<devtools::Preview>(root, L"test.NMV");
        state.view.actual = false;
        state.first_tick = true;
        require(CreateWindowExW(0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                0, 0, 760, 600, nullptr, nullptr, state.module, &state) != nullptr,
                "Cannot create navigation preview");
        KillTimer(state.window, 1);
        require(ListView_GetItemCount(state.frames) == 2 && !IsWindowEnabled(state.previous) &&
                    IsWindowEnabled(state.next),
                "Navigation frame list or initial bounds missing");
        SendMessageW(state.window, WM_COMMAND, devtools::standalone::previous_id, 0);
        require(state.player->image()->sample == 0, "Previous underflowed first frame");
        SendMessageW(state.window, WM_COMMAND, devtools::standalone::next_id, 0);
        pixels(state, RGB(255, 0, 0));
        require(state.player->direct_frame() && !state.player->playing() &&
                    ListView_GetNextItem(state.frames, -1, LVNI_SELECTED) == 1 &&
                    !IsWindowEnabled(state.next),
                "Next did not select paused last frame");
        SendMessageW(state.window, WM_COMMAND, devtools::standalone::next_id, 0);
        require(state.player->image()->sample == 1, "Next exceeded final frame");
        ValidateRect(state.picture, nullptr);
        for (unsigned tick = 0; tick < 50; ++tick) {
            SendMessageW(state.window, WM_TIMER, 1, 0);
        }
        require(state.player->image()->sample == 1 && !GetUpdateRect(state.picture, nullptr, FALSE),
                "Paused navigation selection changed or repainted");
        require(text(state.clock).find(L"Frame 2 / 2") != std::wstring::npos,
                "Direct frame clock did not identify selected frame");
        ListView_SetItemState(state.frames, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        require(state.player->image()->sample == 0 && state.player->direct_frame(),
                "Frame list did not select frame");
        SendMessageW(state.window, WM_COMMAND, devtools::standalone::next_id, 0);
        SendMessageW(state.window, WM_COMMAND, devtools::standalone::play_id, 0);
        require(state.player->playing() && !state.player->direct_frame() &&
                    state.player->image()->sample == 0 &&
                    ListView_GetNextItem(state.frames, -1, LVNI_SELECTED) == 0,
                "Play did not resume normal timeline and sync frame list");
        SendMessageW(state.window, WM_COMMAND, devtools::standalone::stop_id, 0);
        require(!state.player->playing() && !state.player->direct_frame() &&
                    state.player->image()->sample == 0,
                "Stop did not reset navigation timeline");
        state.player->select_frame(1);
        state.player->select_frame(2);
        require(state.player->image()->sample == 1, "Invalid frame changed selection");
        state.player->seek(0);
        devtools::standalone::preview_tick(state);
        require(!state.player->direct_frame() && state.player->image()->sample == 0,
                "Seek retained direct frame override");
        DestroyWindow(state.window);
        const auto zero_timing = movie(true, false, true);
        std::ofstream(root / L"untimed.nmv", std::ios::binary)
            .write(reinterpret_cast<const char*>(zero_timing.data()),
                   static_cast<std::streamsize>(zero_timing.size()));
        devtools::Preview untimed(root, L"untimed.nmv");
        require(untimed.image() && untimed.image()->sample == 0,
                "Untimed navigation first frame was unavailable");
        untimed.select_frame(1);
        untimed.update();
        require(untimed.image() && untimed.image()->sample == 1,
                "Untimed navigation selection was replaced by time lookup");
        const auto multi_data = movie(false, true);
        std::ofstream(root / L"multiple.nmv", std::ios::binary)
            .write(reinterpret_cast<const char*>(multi_data.data()),
                   static_cast<std::streamsize>(multi_data.size()));
        state.player = std::make_unique<devtools::Preview>(root, L"multiple.nmv");
        state.first_tick = true;
        require(state.player->video_tracks().size() == 2 && state.player->video_track() == 1,
                "Preview did not prefer enabled video track after disabled track");
        require(CreateWindowExW(0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                0, 0, 760, 600, nullptr, nullptr, state.module, &state) != nullptr,
                "Cannot create multiple-track preview");
        KillTimer(state.window, 1);
        require(SendMessageW(state.tracks, CB_GETCOUNT, 0, 0) == 2 &&
                    SendMessageW(state.tracks, CB_GETCURSEL, 0, 0) == 1 &&
                    ListView_GetItemCount(state.frames) == 1,
                "Track chooser default incorrect");
        pixels(state, RGB(255, 0, 0));
        SendMessageW(state.tracks, CB_SETCURSEL, 0, 0);
        SendMessageW(state.window, WM_COMMAND,
                     MAKEWPARAM(devtools::standalone::tracks_id, CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(state.tracks));
        require(state.player->video_track() == 0 && state.player->image()->sample == 0 &&
                    state.painted_track == 0 && ListView_GetItemCount(state.frames) == 2 &&
                    !state.player->playing(),
                "Track switch retained sample cache or stale list");
        pixels(state, RGB(0, 255, 0));
        state.player->select_video_track(999);
        require(state.player->video_track() == 0, "Invalid track changed selection");
        DestroyWindow(state.window);
        const auto gap_data = movie(false, false, false, true);
        std::ofstream(root / L"gap.nmv", std::ios::binary)
            .write(reinterpret_cast<const char*>(gap_data.data()),
                   static_cast<std::streamsize>(gap_data.size()));
        state.player = std::make_unique<devtools::Preview>(root, L"gap.nmv");
        state.first_tick = true;
        require(CreateWindowExW(0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                0, 0, 760, 600, nullptr, nullptr, state.module, &state) != nullptr,
                "Cannot create edit-gap preview");
        KillTimer(state.window, 1);
        state.player->seek(0);
        devtools::standalone::preview_tick(state);
        require(!state.player->image() &&
                    ListView_GetNextItem(state.frames, -1, LVNI_SELECTED) == -1 &&
                    !IsWindowEnabled(state.previous) && !IsWindowEnabled(state.next),
                "Edit gap retained stale frame selection or navigation buttons");
        state.player->select_frame(1);
        devtools::standalone::preview_tick(state);
        pixels(state, RGB(255, 0, 0));
        require(ListView_GetNextItem(state.frames, -1, LVNI_SELECTED) == 1,
                "Direct selection failed across an edit gap");
        DestroyWindow(state.window);
        std::filesystem::remove_all(root);
        for (int argument = 1; argument + 1 < argc; ++argument) {
            if (std::string(argv[argument]) != "--root") {
                continue;
            }
            const std::filesystem::path installation(argv[++argument]);
            std::size_t total = 0;
            for (const auto* name : {L"NAV1.NMV", L"NAV2.NMV", L"NAV3.NMV", L"NAV4.NMV",
                                     L"NAV5.NMV", L"NAV6.NMV", L"NAV7.NMV", L"NAVM.NMV"}) {
                devtools::Preview navigation(installation, name);
                require(navigation.frame_navigation() && navigation.frame_count(),
                        "Installed navigation archive has no frames");
                for (const auto track_index : navigation.video_tracks()) {
                    navigation.select_video_track(track_index);
                    for (std::size_t sample = 0; sample < navigation.frame_count(); ++sample) {
                        navigation.select_frame(sample);
                        navigation.update();
                        const auto reference = navigation.image();
                        const auto [width, height] = navigation.frame_dimensions(sample);
                        require(reference && reference->sample == sample &&
                                    navigation.direct_frame() && !navigation.playing() &&
                                    navigation.frame().width == width &&
                                    navigation.frame().height == height,
                                "Installed navigation frame did not decode or retain selection");
                    }
                    total += navigation.frame_count();
                    std::cout << installation.string() << '/'
                              << std::filesystem::path(name).string() << " track "
                              << track_index + 1 << ": " << navigation.frame_count()
                              << " decoded frames\n";
                }
            }
            std::cout << installation.string() << ": all " << total
                      << " navigation frames passed\n";
        }
        std::cout << "Standalone preview redraw, frame navigation and size modes passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (state.window) {
            DestroyWindow(state.window);
        }
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
