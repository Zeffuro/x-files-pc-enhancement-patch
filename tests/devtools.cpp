#include "devtools/preview.h"
#include "devtools/caption_index.h"
#include "devtools/message_pump.h"
#include "devtools/subtitle_time.h"
#include "devtools/artwork.h"
#include "devtools/variables.h"
#include "game/layouts/database/variable.h"
#include "game/layouts/input/inventory_selection.h"
#include "game/profiles/variables.h"
#include <cstring>
#include "devtools/clip_defaults.h"
#include "devtools/inspector_preview_details.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Data = std::vector<std::uint8_t>;
int outputs = 0, starts = 0;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

unsigned tool_messages = 0, tool_closes = 0;
HWND closed_tool = nullptr;
HHOOK quit_hook = nullptr;
constexpr UINT quit_trigger = WM_APP + 47;

void verify_preview_details_tree() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TREEVIEW_CLASSES};
    require(InitCommonControlsEx(&controls), "Cannot initialize preview detail tree");
    const auto parent = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 300, 200, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), nullptr);
    const auto tree =
        CreateWindowW(WC_TREEVIEWW, L"", WS_CHILD | TVS_HASBUTTONS | TVS_LINESATROOT, 0, 0, 300,
                      200, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(parent && tree, "Cannot create preview detail tree");
    devtools::inspector::PreviewDetailsTree details;
    std::vector<devtools::inspector::PreviewDetail> rows{
        {L"clip", L"", L"Clip", true},
        {L"clip/path", L"clip", L"Path: XN/7.xmv"},
        {L"playback", L"", L"Playback", true},
        {L"playback/state", L"playback", L"State: Paused"}};
    details.update(tree, rows);
    const auto clip = TreeView_GetRoot(tree);
    const auto playback = TreeView_GetNextSibling(tree, clip);
    const auto selected = TreeView_GetChild(tree, playback);
    require(clip && playback && selected &&
                (TreeView_GetItemState(tree, clip, TVIS_EXPANDED) & TVIS_EXPANDED),
            "Preview detail group did not expand after children were inserted");
    TreeView_Expand(tree, clip, TVE_COLLAPSE);
    TreeView_SelectItem(tree, selected);
    rows.back().text = L"State: Playing";
    details.update(tree, rows);
    require(TreeView_GetSelection(tree) == selected && TreeView_GetRoot(tree) == clip &&
                TreeView_GetChild(tree, playback) == selected &&
                !(TreeView_GetItemState(tree, clip, TVIS_EXPANDED) & TVIS_EXPANDED),
            "Periodic preview details changed selection or a collapsed group");
    wchar_t text[64]{};
    TVITEMW item{};
    item.mask = TVIF_TEXT;
    item.hItem = selected;
    item.pszText = text;
    item.cchTextMax = static_cast<int>(std::size(text));
    require(TreeView_GetItem(tree, &item) && std::wstring(text) == L"State: Playing",
            "Changed playback detail was not updated in place");
    details.update(tree, rows);
    require(TreeView_GetCount(tree) == 4 && TreeView_GetSelection(tree) == selected,
            "Unchanged preview details rebuilt the tree");
    rows.pop_back();
    details.update(tree, rows);
    require(TreeView_GetCount(tree) == 3 && TreeView_GetRoot(tree) == clip &&
                !(TreeView_GetItemState(tree, clip, TVIS_EXPANDED) & TVIS_EXPANDED),
            "Removing obsolete details rebuilt preserved groups");
    rows.push_back({L"playback/empty", L"playback", L"No open preview"});
    details.update(tree, rows);
    require(TreeView_GetCount(tree) == 4 && TreeView_GetNextSibling(tree, clip) == playback,
            "New preview detail lost its existing parent");
    DestroyWindow(parent);
}

LRESULT CALLBACK quit_between_peeks(int code, WPARAM remove, LPARAM data) {
    if (code >= 0 && remove == PM_NOREMOVE) {
        const auto& message = *reinterpret_cast<const MSG*>(data);
        if (message.message == quit_trigger) {
            UnhookWindowsHookEx(quit_hook);
            quit_hook = nullptr;
            MSG removed{};
            PeekMessageW(&removed, message.hwnd, quit_trigger, quit_trigger, PM_REMOVE);
            PostQuitMessage(73);
        }
    }
    return CallNextHookEx(nullptr, code, remove, data);
}

void require_quit(WPARAM exit_code) {
    MSG message{};
    // PostQuitMessage waits until posted window notifications have been removed.
    for (unsigned count = 0; count < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE);
         ++count) {
        if (message.message == WM_QUIT) {
            require(message.wParam == exit_code, "Tool pump changed the quit exit code");
            return;
        }
        DispatchMessageW(&message);
    }
    require(false, "Tool pump consumed WM_QUIT");
}

LRESULT CALLBACK tool_window(HWND window, UINT message, WPARAM value, LPARAM data) {
    if (message == WM_CLOSE) {
        ++tool_closes;
        closed_tool = window;
        return 0;
    }
    if (message == WM_LBUTTONUP || message == WM_TIMER) {
        ++tool_messages;
        return 0;
    }
    return DefWindowProcW(window, message, value, data);
}

void verify_tool_pump() {
    WNDCLASSW type{};
    type.lpfnWndProc = tool_window;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"XFilesToolPumpTest";
    require(RegisterClassW(&type) != 0, "Cannot register tool test window");
    const auto game = CreateWindowW(type.lpszClassName, L"", 0, 0, 0, 0, 0, nullptr, nullptr,
                                    type.hInstance, nullptr);
    const auto tools = CreateWindowW(type.lpszClassName, L"", 0, 0, 0, 0, 0, nullptr, nullptr,
                                     type.hInstance, nullptr);
    const auto child = CreateWindowW(type.lpszClassName, L"", WS_CHILD, 0, 0, 0, 0, tools, nullptr,
                                     type.hInstance, nullptr);
    require(game && tools && child, "Cannot create tool test windows");
    require(PostMessageW(game, WM_KEYDOWN, VK_F8, 0) && PostMessageW(child, WM_LBUTTONUP, 0, 0) &&
                PostMessageW(tools, WM_TIMER, 1, 0),
            "Cannot queue tool test messages");
    devtools::pump_tool_messages(nullptr);
    require(tool_messages == 0, "Missing tools window drained the thread queue");
    for (unsigned i = 0; i < 4; ++i) {
        devtools::pump_tool_messages(tools);
    }
    require(tool_messages == 2, "Queued game input starved tool child input or timers");
    const auto nested = CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"", WS_CHILD, 0,
                                        0, 0, 0, child, nullptr, type.hInstance, nullptr);
    const auto edit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_TABSTOP, 0, 0, 0, 0, nested,
                                    nullptr, type.hInstance, nullptr);
    require(nested && edit && PostMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0),
            "Cannot queue nested tool Escape");
    devtools::pump_tool_messages(tools);
    require(tool_closes == 1 && closed_tool == tools, "Nested Escape did not close the tools root");
    MSG message{};
    require(PeekMessageW(&message, game, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE),
            "Tool pump consumed native game input");
    PostQuitMessage(42);
    devtools::pump_tool_messages(tools);
    require_quit(42);
    require(PostMessageW(tools, quit_trigger, 0, 0), "Cannot queue quit trigger");
    quit_hook = SetWindowsHookExW(WH_GETMESSAGE, quit_between_peeks, nullptr, GetCurrentThreadId());
    require(quit_hook != nullptr, "Cannot install quit test hook");
    devtools::pump_tool_messages(tools);
    if (quit_hook) {
        UnhookWindowsHookEx(quit_hook);
    }
    require(quit_hook == nullptr, "Quit test hook did not run");
    require_quit(73);
    DestroyWindow(tools);
    DestroyWindow(game);
    UnregisterClassW(type.lpszClassName, type.hInstance);
}

void word(Data& data, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        data.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

Data words(std::initializer_list<std::uint32_t> values) {
    Data data;
    for (auto value : values) {
        word(data, value);
    }
    return data;
}

void atom(Data& data, const char* name, const Data& content) {
    word(data, static_cast<std::uint32_t>(content.size() + 8));
    data.insert(data.end(), name, name + 4);
    data.insert(data.end(), content.begin(), content.end());
}

Data movie(bool sound, bool fractional = false, bool edited = false) {
    Data result;
    atom(result, "mdat",
         sound ? Data(1000, 128)
               : Data{0xe1, 0, 0, 7, 0xa0, 0x03, 0xe0, 0xe1, 0, 0, 7, 0xa0, 0x7c, 0});
    Data description(sound ? 28 : 78);
    description[7] = 1;
    if (sound) {
        description[17] = 1;
        description[19] = 8;
        description[24] = 3;
        description[25] = 232;
    } else {
        description[25] = description[27] = 4;
    }
    auto descriptions = words({0, 1});
    atom(descriptions, sound ? "raw " : "rpza", description);
    Data samples;
    atom(samples, "stsd", descriptions);
    atom(samples, "stco", words({0, 1, 8}));
    atom(samples, "stsc", words({0, 1, 1, sound ? 1000u : 2u, 1}));
    atom(samples, "stsz", words({0, sound ? 1u : 7u, sound ? 1000u : 2u}));
    atom(samples, "stts", words({0, 1, sound ? 1000u : 2u, sound ? 1u : edited ? 3030u : 500u}));
    Data info, media, track, body;
    atom(info, "stbl", samples);
    atom(media, "mdhd", words({0, 0, 0, edited ? 600u : 1000u, edited ? 6060u : 1000u}));
    atom(media, "hdlr", words({0, 0, sound ? 0x736f756eu : 0x76696465u}));
    atom(media, "minf", info);
    atom(track, "tkhd", words({1, 0, 0, 1, 0, edited ? 6060u : 1000u}));
    if (edited) {
        Data edits;
        atom(edits, "elst", words({0, 1, 6060, 0, 65536}));
        atom(track, "edts", edits);
    }
    atom(track, "mdia", media);
    atom(body, "mvhd",
         words({0, 0, 0,
                edited       ? 600u
                : fractional ? 3000u
                             : 1000u,
                edited       ? 6060u
                : fractional ? 3001u
                             : 1000u}));
    atom(body, "trak", track);
    atom(result, "moov", body);
    return result;
}

void write(const std::filesystem::path& path, const Data& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}
}

// This sink exercises playback requests without opening an audio device.
namespace playback {
struct Output::State {};

Output::Output(const WAVEFORMATEX&) {
    ++outputs;
}

Output::~Output() = default;

bool Output::current_device() const {
    return true;
}

void Output::play(std::span<const std::int16_t>) {
    ++starts;
}

void Output::stop() {}

void Output::volume(std::int16_t, std::int16_t) {}

void Output::speed(unsigned) {}
}

int main() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("xfiles-preview-" + std::to_string(GetCurrentProcessId()));
    try {
        verify_tool_pump();
        verify_preview_details_tree();
        require(fs::create_directory(root), "Test directory exists");
        write(root / "video.xmv", movie(false));
        write(root / "audio.amv", movie(true));
        write(root / "fractional.xmv", movie(false, true));
        write(root / "edited.xmv", movie(false, false, true));
        devtools::Preview fractional(root, "fractional.xmv");
        require(fractional.duration() == 1001, "Fractional movie end was truncated for SRT timing");
        devtools::Preview edited(root, "edited.xmv");
        require(edited.duration() == 10100, "Edited movie duration was converted incorrectly");
        edited.seek(8000);
        require(edited.image() && edited.image()->sample == 1 && !edited.frame().pixels.empty(),
                "Edited movie lost its final frame after a millisecond seek");
        devtools::Preview video(root, "video.xmv");
        require(video.has_video() && !video.has_audio() && !video.playing(),
                "Wrong initial video state");
        require(video.image() && video.image()->track == 1 && video.image()->sample == 0 &&
                    video.image()->count == 2,
                "First decoded image has the wrong identity");
        require(media::navigation_archive(L"NAVM.NMV") && media::navigation_archive(L"nav7.nmv") &&
                    !media::navigation_archive(L"nav8.nmv") &&
                    !media::navigation_archive(L"XV/123.xmv"),
                "Navigation archive classification is wrong");
        require(media::frame_key(L"NAVM.NMV", *video.image()) == L"navm.nmv#track=1&sample=0",
                "Navigation image key is not stable");
        require(!media::frame_reference(video.movie().tracks.front(), 2),
                "Invalid sample got an image identity");
        const auto first = video.frame().pixels;
        require(first.size() == 64 && first[1] > 240, "First frame was not decoded");
        video.seek(600);
        require(video.frame().pixels[2] > 240, "Seek did not decode second frame");
        require(video.image()->sample == 1, "Seek did not update decoded image identity");
        video.seek(0);
        require(video.image()->sample == 0, "Backward seek retained stale image identity");
        require(video.frame().pixels == first, "Backward seek did not reproduce first frame");
        video.captions({{100, 200, L"Test caption"}});
        video.seek(150);
        require(video.caption() == L"Test caption", "Preview caption clock mismatch");
        video.play();
        video.pause();
        const auto stopped = video.time();
        video.update();
        require(video.time() == stopped, "Paused preview advanced");
        video.seek(999999);
        require(video.time() == 1000 && !video.playing(), "Seek beyond duration was not clamped");
        devtools::Preview audio(root, "audio.amv");
        require(audio.has_audio() && !audio.has_video() && audio.frame().pixels.empty(),
                "Audio-only metadata wrong");
        require(!audio.image(), "Audio-only movie has a video image identity");
        require(outputs == 0, "Loading a movie opened audio output");
        {
            devtools::ArtworkCache artwork(root);
            artwork.request({"video.xmv", "audio.amv", "missing.xmv"});
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!artwork.get("missing.xmv") && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const auto picture = artwork.get("video.xmv"), sound = artwork.get("audio.amv"),
                       missing = artwork.get("missing.xmv");
            require(picture && !picture->frame.pixels.empty() && !picture->audio,
                    "Background thumbnail did not decode video");
            require(sound && sound->audio && sound->frame.pixels.empty(),
                    "Background thumbnail misclassified audio");
            require(missing && missing->failed, "Unreadable thumbnail was not recorded");
            require(outputs == 0 && starts == 0, "Thumbnail loading started audio");
        }
        {
            media::subtitles::install_text(root, "video.xmv",
                                           "1\n00:00:00,100 --> 00:00:00,200\nCustom caption\n");
            devtools::CaptionIndex index;
            require(index.coverage("video.xmv") == devtools::CaptionCoverage::pending,
                    "Unindexed clip was classified as having no captions");
            index.start(root, {"video.xmv", "audio.amv", "missing.xmv"});
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (index.completed() != 3 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            require(index.coverage("VIDEO.XMV") == devtools::CaptionCoverage::present &&
                        index.contains("video.xmv", L"custom caption") &&
                        index.coverage("audio.amv") == devtools::CaptionCoverage::none &&
                        index.coverage("missing.xmv") == devtools::CaptionCoverage::unreadable,
                    "Caption coverage confused overrides, empty captions or unreadable clips");
        }
        audio.play();
        require(outputs == 1 && starts == 1, "Explicit preview did not start audio");
        audio.pause();
        native_game::Variable variable{};
        variable.id = 12345;
        variable.raw_value = -7;
        variable.type_flags = 0x82;
        for (const auto* profile :
             {&native_game::profile_cd_10012, &native_game::profile_cd_10019,
              &native_game::profile_cd_10020, &native_game::profile_dvd_20000}) {
            std::vector<std::byte> image(0x300000);
            const auto pointer = reinterpret_cast<std::uint32_t>(&variable);
            std::memcpy(image.data() + native_game::registered_variables[0].rva(*profile), &pointer,
                        sizeof(pointer));
            const auto snapshot = devtools::inspect_variables(image.data(), *profile);
            require(snapshot.find(L"12345 / -7 (0xfffffff9) / 2") != std::wstring::npos,
                    "Registered variable snapshot read the wrong layout");
            require(variable.raw_value == -7 && variable.type_flags == 0x82,
                    "Snapshot changed story state");
            const std::uint32_t invalid = 1;
            std::memcpy(image.data() + native_game::registered_variables[0].rva(*profile), &invalid,
                        sizeof(invalid));
            require(devtools::inspect_variables(image.data(), *profile)
                            .find(L"RegUberVars [1]: unavailable (unreadable object)") !=
                        std::wstring::npos,
                    "Invalid variable pointer was not handled");
            const auto& selection =
                native_game::registered_variables[std::size(native_game::registered_variables) - 1];
            std::memcpy(image.data() + selection.rva(*profile), &pointer, sizeof(pointer));
            variable.raw_value = 9;
            variable.type_flags = 0x81;
            require(devtools::inspect_variables(image.data(), *profile).find(L" / 1 - Gun") !=
                        std::wstring::npos,
                    "Verified script inventory selection was not named");
            variable.type_flags = 0x82;
            require(devtools::inspect_variables(image.data(), *profile).find(L" - Gun") ==
                        std::wstring::npos,
                    "Noninteger variable was interpreted as an inventory selection");
            variable.raw_value = -7;
        }
        require(native_game::inventory_selection_name(0) == L"None" &&
                    native_game::inventory_selection_name(9) == L"Gun" &&
                    native_game::inventory_selection_name(12) == L"Lockpick" &&
                    native_game::inventory_selection_name(2).empty() &&
                    native_game::inventory_selection_name(3) == L"PDA" &&
                    native_game::inventory_selection_name(1).empty() &&
                    native_game::inventory_selection_name(0x1be8).empty(),
                "Inventory selection enum confused a graphic resource or unknown value");
        for (const auto* text : {L"00:00:10,3", L"10.3", L"0:10.300", L" 10,300 "}) {
            require(devtools::subtitle_time(text) == 10300, "Flexible subtitle time mismatch");
        }
        require(devtools::subtitle_time(L"1:02:03.04") == 3723040, "Hour subtitle time mismatch");
        for (const auto* text : {L"", L"1:60", L"1:60:00", L"-1", L"1.1234", L"1:", L"nan"}) {
            bool rejected_time = false;
            try {
                devtools::subtitle_time(text);
            } catch (const std::exception&) {
                rejected_time = true;
            }
            require(rejected_time, "Invalid subtitle time accepted");
        }
        auto labels = devtools::parse_defaults(
            "path\tlabel\tcomment\nxn/38844.xmv\tMark Cook\tAt his desk\n");
        require(labels.at(L"xn/38844.xmv").label == L"Mark Cook", "Default label missing");
        const auto bundled = devtools::load_defaults(root);
        require(bundled.contains(L"navm.nmv") &&
                    bundled.at(L"xn/38844.xmv").label.find(L"Mark Cook") != std::wstring::npos,
                "Bundled catalog could not be loaded");
        const auto corrected = devtools::parse_defaults(
            "path\tlabel\tcomment\tplace\nXN/26240.xmv\tHauling Yard\tView\tHauling Yard\n"
            "XV/19650.xmv\tGame over\tDeath\t-\n");
        require(corrected.at(L"xn/26240.xmv").place == L"Hauling Yard" &&
                    corrected.at(L"xv/19650.xmv").place == L"",
                "Explicit place corrections were lost");
        const auto windows_labels = devtools::parse_defaults(
            "\xef\xbb\xbfpath\tlabel\tcomment\r\nxn/1.xmv\tTest\tNote\r\n");
        require(windows_labels.size() == 1, "Windows text catalog was rejected");
        bool rejected = false;
        try {
            devtools::parse_defaults("path\tlabel\tcomment\n../bad\tLabel\tNotes\n");
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Invalid catalog path accepted");
        fs::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
