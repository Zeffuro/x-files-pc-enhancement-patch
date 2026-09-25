#include "playback/caption_text.h"
#include "dispatch.h"
#include "playback/menu_colors.h"
#include "playback/components.h"
#include "playback/volume.h"
#include "settings.h"
#include "quickdraw/types.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using namespace test;
using Data = std::vector<std::uint8_t>;

void verify_menu_black() {
    Data pixels{2, 2, 2, 255, 7, 7, 7, 255, 8, 8, 8, 255, 3, 2, 2, 255};
    playback::restore_menu_black(pixels);
    require(pixels == Data{0, 0, 0, 255, 0, 0, 0, 255, 8, 8, 8, 255, 3, 2, 2, 255},
            "Menu black correction changed the glow or alpha channel.");
    require(playback::is_menu_animation("49587.xmv") && playback::is_menu_animation("49587.XMV") &&
                !playback::is_menu_animation("49583.xmv"),
            "Menu correction selected unrelated movie artwork.");
}

void verify_balance(std::uint32_t track) {
    using namespace playback;
    require(stereo_volume(256, 0) == 0xffffffff && stereo_volume(256, -128) == 0x0000ffff &&
                stereo_volume(256, 127) == 0xffff0000 && stereo_volume(0, 40) == 0 &&
                stereo_volume(128, 0) == 0x7fff7fff,
            "Stereo balance or master volume is incorrect.");
    const auto handler = invoke(Selector::GetMediaHandler, invoke(Selector::GetTrackMedia, track));
    require(handler != 0, "Audio media handler is missing.");
    ComponentParameters parameters{0, 4, MediaSelector::SetSoundBalance, 127};
    require(invoke(Selector::CallComponent, handler, address(&parameters)) == 0,
            "MediaSetSoundBalance failed.");
    std::int16_t balance = 0;
    parameters.selector = MediaSelector::GetSoundBalance;
    parameters.argument = address(&balance);
    using ComponentCall = std::int32_t(__cdecl*)(void*, const ComponentParameters*);
    const auto exported = reinterpret_cast<ComponentCall>(
        GetProcAddress(GetModuleHandleW(L"QuickTime.qts"), "_CallComponent"));
    require(exported && exported(reinterpret_cast<void*>(handler), &parameters) == 0 &&
                balance == 127,
            "Exported component dispatch lost track balance.");
    parameters.selector = MediaSelector::SetSoundBalance;
    parameters.argument = 128;
    require(static_cast<std::int32_t>(
                invoke(Selector::CallComponent, handler, address(&parameters))) == -50,
            "Invalid sound balance was accepted.");
    parameters.argument = 0;
    require(invoke(Selector::CallComponent, handler, address(&parameters)) == 0,
            "Cannot restore neutral sound balance.");
    parameters.size = 0;
    require(static_cast<std::int32_t>(
                invoke(Selector::CallComponent, handler, address(&parameters))) == -50,
            "Malformed component parameters were accepted.");
}

Data words(std::initializer_list<std::uint32_t> values) {
    Data result;
    for (auto value : values) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            result.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }
    return result;
}

void atom(Data& output, const char* type, const Data& body) {
    const auto size = words({static_cast<std::uint32_t>(body.size() + 8)});
    output.insert(output.end(), size.begin(), size.end());
    output.insert(output.end(), type, type + 4);
    output.insert(output.end(), body.begin(), body.end());
}

Data fixture(bool video = false, bool captions = false, bool overlay = false) {
    Data file;
    auto packets = video ? Data{0xe1, 0, 0, 7, 0xa0, 0x03, 0xe0} : Data(34 * 64);
    if (captions) {
        const Data text{0, 5, 'H', 'e', 'l', 'l', 'o'};
        packets.insert(packets.end(), text.begin(), text.end());
        if (overlay) {
            const Data correction{0, 3, 'B', 'y', 'e'};
            packets.insert(packets.end(), correction.begin(), correction.end());
        }
    }
    atom(file, "mdat", packets);
    Data description = words({0, 1, 0, 0, 0x00010010, 0, 8000u << 16});
    if (video) {
        description.assign(78, 0);
        description[7] = 1;
        description[25] = description[27] = 4;
        description[75] = 16;
    }
    Data descriptions = words({0, 1});
    atom(descriptions, video ? "rpza" : "ima4", description);
    Data samples;
    atom(samples, "stsd", descriptions);
    atom(samples, "stco", words({0, 1, 8}));
    atom(samples, "stsc", words({0, 1, 1, video ? 1u : 4096u, 1}));
    atom(samples, "stsz", video ? words({0, 7, 1}) : words({0, 1, 4096}));
    atom(samples, "stts", video ? words({0, 1, 1, 500}) : words({0, 1, 4096, 1}));
    Data info;
    atom(info, "stbl", samples);
    Data media;
    atom(media, "mdhd", video ? words({0, 0, 0, 1000, 500}) : words({0, 0, 0, 8000, 4096}));
    atom(media, "hdlr", words({0, 0, video ? 0x76696465u : 0x736f756eu}));
    atom(media, "minf", info);
    Data edits;
    atom(edits, "elst", words({0, 2, 20, 0xffffffff, 65536, 480, 0, 65536}));
    Data track;
    atom(track, "tkhd", words({1, 0, 0, 1, 0, 500}));
    atom(track, "edts", edits);
    atom(track, "mdia", media);
    Data movie;
    atom(movie, "mvhd", words({0, 0, 0, 1000, 500}));
    atom(movie, "trak", track);
    for (unsigned index = 0; index < (captions ? (overlay ? 2u : 1u) : 0u); ++index) {
        Data caption_descriptions = words({0, 1});
        atom(caption_descriptions, "text", words({0, 1}));
        Data caption_samples;
        atom(caption_samples, "stsd", caption_descriptions);
        atom(caption_samples, "stco", words({0, 1, index ? 22u : 15u}));
        atom(caption_samples, "stsc", words({0, 1, 1, 1, 1}));
        atom(caption_samples, "stsz", words({0, index ? 5u : 7u, 1}));
        atom(caption_samples, "stts", words({0, 1, 1, 250}));
        Data caption_info;
        atom(caption_info, "stbl", caption_samples);
        Data caption_media;
        atom(caption_media, "mdhd", words({0, 0, 0, 1000, 250}));
        atom(caption_media, "hdlr", words({0, 0, 0x74657874}));
        atom(caption_media, "minf", caption_info);
        Data caption_track;
        atom(caption_track, "tkhd",
             words({1, 0, 0, index + 2, 0, 250, 0, 0,          0,         0,        65536,
                    0, 0, 0, 65536,     0, 0,   0, 0x40000000, 80u << 16, 20u << 16}));
        atom(caption_track, "mdia", caption_media);
        atom(movie, "trak", caption_track);
    }
    atom(file, "moov", movie);
    return file;
}

#pragma pack(push, 2)

struct FileSpec {
    std::int16_t volume = 0;
    std::int32_t directory = 0;
    std::uint8_t length = 0;
    char name[255]{};
};

struct TimeRecord {
    std::uint32_t low;
    std::int32_t high;
    std::int32_t scale;
    void* base;
};

#pragma pack(pop)

unsigned callbacks = 0;

void __cdecl complete(void* callback, std::int32_t context) {
    require(context == 1234, "Callback context was corrupted.");
    ++callbacks;
    invoke(Selector::DisposeCallBack, address(callback));
}

constexpr UINT dispose_message = WM_APP + 1;
unsigned dispatched = 0;

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM value, LPARAM parameter) {
    if (message == dispose_message) {
        ++dispatched;
        invoke(Selector::MoviesTask, static_cast<std::uint32_t>(value));
        invoke(Selector::DisposeMovie, static_cast<std::uint32_t>(value));
        return 0;
    }
    return DefWindowProcW(window, message, value, parameter);
}

void verify_message_pump(std::uint32_t movie) {
    WNDCLASSW type{};
    type.lpfnWndProc = window_proc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"XFilesMoviePumpTest";
    require(RegisterClassW(&type) != 0, "Cannot register playback test window.");
    const auto window = CreateWindowW(type.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                      type.hInstance, nullptr);
    require(window != nullptr, "Cannot create playback test window.");
    for (const auto input : {WM_KEYDOWN, WM_LBUTTONDOWN, WM_LBUTTONUP}) {
        require(PostMessageW(window, input, 0, 0), "Cannot post input message.");
        invoke(Selector::MoviesTask, movie);
        MSG message{};
        require(PeekMessageW(&message, window, input, input, PM_REMOVE),
                "MoviesTask consumed input before the game's event loop.");
    }
    require(PostMessageW(window, dispose_message, movie, 0), "Cannot post playback message.");
    invoke(Selector::MoviesTask, movie);
    require(dispatched == 1, "MoviesTask did not service the message queue.");
    PostQuitMessage(42);
    invoke(Selector::MoviesTask);
    MSG message{};
    require(PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) && message.wParam == 42,
            "MoviesTask swallowed WM_QUIT.");
    DestroyWindow(window);
    UnregisterClassW(type.lpszClassName, type.hInstance);
}

void verify_external(const std::filesystem::path& path) {
    FileSpec file;
    const auto name = path.string();
    require(name.size() <= sizeof(file.name), "Fixture path is too long.");
    file.length = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), file.name);
    short reference = 0;
    require(static_cast<short>(
                invoke(Selector::OpenMovieFile, address(&file), address(&reference), 1)) == 0,
            "OpenMovieFile failed.");
    void* movie = nullptr;
    require(static_cast<short>(
                invoke(Selector::NewMovieFromFile, address(&movie), reference, 0, 0, 1)) == 0 &&
                movie,
            "NewMovieFromFile failed.");
    require(static_cast<short>(invoke(Selector::CloseMovieFile, reference)) == 0,
            "CloseMovieFile failed.");
    const auto handle = address(movie);
    invoke(Selector::SetMovieVolume, handle, 0);
    invoke(Selector::StartMovie, handle);
    invoke(Selector::StopMovie, handle);
    invoke(Selector::DisposeMovie, handle);
}

void verify(const std::filesystem::path& path, bool audio_output) {
    FileSpec file;
    const auto name = path.string();
    require(name.size() <= sizeof(file.name), "Fixture path is too long.");
    file.length = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), file.name);
    short reference = 0;
    require(static_cast<short>(
                invoke(Selector::OpenMovieFile, address(&file), address(&reference), 1)) == 0,
            "OpenMovieFile failed.");
    void* movie = nullptr;
    require(static_cast<short>(
                invoke(Selector::NewMovieFromFile, address(&movie), reference, 0, 0, 1)) == 0 &&
                movie,
            "NewMovieFromFile failed.");
    require(static_cast<short>(invoke(Selector::CloseMovieFile, reference)) == 0,
            "CloseMovieFile failed.");
    const auto handle = address(movie);
    require(invoke(Selector::GetMovieDuration, handle) == 500 &&
                invoke(Selector::GetMovieTimeScale, handle) == 1000,
            "Movie timing metadata is incorrect.");
    require(invoke(Selector::GetMovieTrackCount, handle) == 1, "Track count is incorrect.");
    const auto track = invoke(Selector::GetMovieIndTrack, handle, 1);
    require(track && !invoke(Selector::GetMovieIndTrack, handle, 2),
            "Track indexing is incorrect.");
    require(invoke(Selector::GetTrackOffset, track) == 20 &&
                invoke(Selector::GetTrackDuration, track) == 500,
            "Track timing must use movie units and retain the initial empty edit.");
    require(static_cast<std::uint8_t>(invoke(Selector::GetTrackEnabled, track)) == 1,
            "Track enable flag was lost.");
    require(static_cast<short>(invoke(Selector::LoadTrackIntoRam, track, 0, 500, 1)) == 0 &&
                static_cast<short>(invoke(Selector::LoadTrackIntoRam, track, 500, 1, 1)) == -50,
            "Track cache hints did not validate the requested time range.");
    std::uint32_t type = 0;
    invoke(Selector::GetMediaHandlerDescription, invoke(Selector::GetTrackMedia, track),
           address(&type));
    require(type == 0x736f756e, "Audio handler type is incorrect.");
    verify_balance(track);
    invoke(Selector::SetMovieSelection, handle, 20, 100);
    std::int32_t start = 0, duration = 0;
    invoke(Selector::GetMovieSelection, handle, address(&start), address(&duration));
    require(start == 20 && duration == 100, "Movie selection was not preserved.");
    const auto base = invoke(Selector::GetMovieTimeBase, handle);
    const auto callback = invoke(Selector::NewCallBack, base, 1);
    require(static_cast<short>(
                invoke(Selector::CallMeWhen, callback, address(complete), 1234, 1, 500, 1000)) == 0,
            "Completion callback registration failed.");
    invoke(Selector::SetMovieVolume, handle, 0);
    invoke(Selector::SetMovieActive, handle, 0);
    require(static_cast<std::uint8_t>(invoke(Selector::GetMovieActive, handle)) == 0,
            "Movie was not deactivated.");
    if (!audio_output) {
        invoke(Selector::SetTrackEnabled, track, 0);
    }
    invoke(Selector::StartMovie, handle);
    require(static_cast<std::uint8_t>(invoke(Selector::GetMovieActive, handle)) == 1,
            "StartMovie did not reactivate the movie.");
    invoke(Selector::SetTrackEnabled, track, 0);
    require(static_cast<std::uint8_t>(invoke(Selector::GetTrackEnabled, track)) == 0 &&
                invoke(Selector::GetMovieRate, handle) == 65536,
            "Disabling audio stopped the movie clock.");
    if (!audio_output) {
        invoke(Selector::StopMovie, handle);
    }
    invoke(Selector::SetTrackEnabled, track, 1);
    verify_balance(track);
    require(static_cast<std::uint8_t>(invoke(Selector::GetTrackEnabled, track)) == 1,
            "Audio track was not reenabled.");
    if (!audio_output) {
        invoke(Selector::SetTrackEnabled, track, 0);
        invoke(Selector::StartMovie, handle);
    }
    Sleep(60);
    invoke(Selector::StopMovie, handle);
    const auto stopped = invoke(Selector::GetMovieTime, handle);
    require(stopped >= 30 && stopped < 500, "Playback clock did not advance.");
    Sleep(20);
    require(invoke(Selector::GetMovieTime, handle) == stopped,
            "Stopped movie continued advancing.");
    invoke(Selector::SetMovieTimeValue, handle, 450);
    TimeRecord time{};
    require(invoke(Selector::GetMovieTime, handle, address(&time)) == 450 && time.low == 450 &&
                time.high == 0 && time.scale == 1000 && address(time.base) == base,
            "TimeRecord ABI or seeking is incorrect.");
    invoke(Selector::StartMovie, handle);
    Sleep(100);
    invoke(Selector::MoviesTask, handle);
    require(invoke(Selector::GetMovieTime, handle) == 500 &&
                invoke(Selector::GetMovieRate, handle) == 0 && callbacks == 1,
            "Movie did not finish and fire its one-shot callback.");
    invoke(Selector::MoviesTask, handle);
    require(callbacks == 1, "Disposed callback fired again.");
    invoke(Selector::SetMovieTimeValue, handle, 0);
    const auto skipped = invoke(Selector::NewCallBack, base, 1);
    require(static_cast<short>(
                invoke(Selector::CallMeWhen, skipped, address(complete), 1234, 1, 500, 1000)) == 0,
            "Skip completion callback registration failed.");
    invoke(Selector::StartMovie, handle);
    invoke(Selector::SetMovieTimeValue, handle, 500);
    invoke(Selector::StopMovie, handle);
    require(callbacks == 1, "Seeking invoked callbacks before movie servicing.");
    invoke(Selector::MoviesTask, handle);
    require(callbacks == 2, "Seeking to the end lost the pending completion callback.");
    invoke(Selector::MoviesTask, handle);
    require(callbacks == 2, "Skip completion callback fired twice.");
    verify_message_pump(handle);
}

void verify_last_frame(const std::filesystem::path& path, bool captions = false,
                       bool overlay = false) {
    const auto data = fixture(true, captions, overlay);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), data.size());
    output.close();
    FileSpec file;
    const auto name = path.string();
    file.length = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), file.name);
    short reference = 0;
    require(static_cast<short>(
                invoke(Selector::OpenMovieFile, address(&file), address(&reference), 1)) == 0,
            "Cannot open video fixture.");
    void* movie = nullptr;
    require(static_cast<short>(
                invoke(Selector::NewMovieFromFile, address(&movie), reference, 0, 0, 1)) == 0,
            "Cannot create video fixture.");
    invoke(Selector::CloseMovieFile, reference);
    quickdraw::Port* port = nullptr;
    const quickdraw::Rect box{0, 0, 60, 80};
    require(static_cast<short>(invoke(Selector::QTNewGWorld, address(&port), quickdraw::bgra_format,
                                      address(&box))) == 0,
            "Cannot create video drawing port.");
    invoke(Selector::SetMovieGWorld, address(movie), address(port));
    if (captions) {
        const quickdraw::Rect picture{0, 0, 30, 80};
        invoke(Selector::SetMovieBox, address(movie), address(&picture));
        const auto text_track = invoke(Selector::GetMovieIndTrack, address(movie), 2);
        require(text_track != 0, "Caption track missing.");
        auto lit_pixels = [&] {
            unsigned count = 0;
            const auto pixels = (*port->pixels)->base;
            for (int i = 0; i < 30 * 80; ++i) {
                count += pixels[i * 4] > 0;
            }
            return count;
        };
        const quickdraw::Rect clipped_picture{0, 0, 25, 80};
        invoke(Selector::SetGWorld, address(port));
        invoke(Selector::ClipRect, address(&clipped_picture));
        invoke(Selector::SetMovieTimeValue, address(movie), 100);
        invoke(Selector::UpdateMovie, address(movie));
        const auto clipped_pixels = lit_pixels();
        require(clipped_pixels > 0, "Caption text was not rendered.");
        if (overlay) {
            const auto pixels = (*port->pixels)->base;
            const Data combined(pixels, pixels + 30 * 80 * 4);
            invoke(Selector::SetTrackEnabled, text_track, 0);
            invoke(Selector::UpdateMovie, address(movie));
            require(std::equal(combined.begin(), combined.end(), pixels),
                    "Overlapping caption tracks doubled the dialogue.");
            invoke(Selector::SetTrackEnabled, text_track, 1);
            const auto correction = invoke(Selector::GetMovieIndTrack, address(movie), 3);
            invoke(Selector::SetTrackEnabled, correction, 0);
            invoke(Selector::UpdateMovie, address(movie));
            require(lit_pixels() > 0 && !std::equal(combined.begin(), combined.end(), pixels),
                    "Disabling the front caption did not reveal the earlier wording.");
        }
        invoke(Selector::ClipRect, address(&box));
        invoke(Selector::UpdateMovie, address(movie));
        require(overlay || lit_pixels() == clipped_pixels,
                "Movie clipping cut off caption glyphs.");
        for (int row = 0; row < 30; ++row) {
            const auto pixels = (*port->pixels)->base + row * 80 * 4;
            require(pixels[1] >= 248 && pixels[79 * 4 + 1] >= 248,
                    "Caption background covered the movie image.");
        }
        invoke(Selector::SetTrackEnabled, text_track, 0);
        invoke(Selector::UpdateMovie, address(movie));
        require(lit_pixels() == 0, "Disabling captions left stale text.");
        invoke(Selector::SetTrackEnabled, text_track, 1);
        invoke(Selector::UpdateMovie, address(movie));
        require(lit_pixels() > 0, "Re-enabling captions lost the current sample.");
        invoke(Selector::StartMovie, address(movie));
        Sleep(180);
        invoke(Selector::MoviesTask, address(movie));
        invoke(Selector::StopMovie, address(movie));
        require(lit_pixels() == 0, "Expired captions remained visible.");
        const auto restored = (*port->pixels)->base;
        for (int i = 0; i < 30 * 80; ++i) {
            require(restored[i * 4 + 1] >= 248,
                    "Caption expiry did not restore the stationary video frame.");
        }
    }
    invoke(Selector::SetMovieTimeValue, address(movie), 500);
    invoke(Selector::UpdateMovie, address(movie));
    const auto pixels = (*port->pixels)->base;
    require(pixels[0] == 0 && pixels[1] >= 248 && pixels[2] == 0,
            "Seeking directly to the movie end did not draw its final frame.");
    invoke(Selector::DisposeMovie, address(movie));
    invoke(Selector::DisposeGWorld, address(port));
}

}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::cerr << "Pass the QuickTime.qts path and optionally --no-audio.\n";
        return 1;
    }
    const bool audio_output = argc < 3 || std::wstring_view(argv[2]) != L"--no-audio";
    const auto library = LoadLibraryW(argv[1]);
    if (!library) {
        std::cerr << "Cannot load QuickTime.qts: " << GetLastError() << '\n';
        return 1;
    }
    dispatcher = GetProcAddress(library, "theQuickTimeDispatcher");
    if (!dispatcher) {
        std::cerr << "QuickTime dispatcher export missing.\n";
        return 1;
    }
    try {
        require(audio_output || argc == 3, "External movie fixtures require audio-output mode.");
        const auto directory = std::filesystem::temp_directory_path() /
                               (L"xfiles-playback-" + std::to_wstring(GetCurrentProcessId()) +
                                L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(directory);
        const auto settings_path = directory / L"patch.ini";
        require(read_settings(settings_path).menu_black_background,
                "Menu black enhancement should default to enabled.");
        require(!read_settings(settings_path).skip_workstation_login,
                "The workstation puzzle must not be skipped by default.");
        require(read_settings(settings_path).analog_cursor,
                "Manual analog pointer control must default to enabled.");
        require(!read_settings(settings_path).spring_cursor,
                "Spring-centred pointer must be opt-in.");
        std::ofstream(settings_path) << "[Input]\nSpringCursor=1\n";
        require(read_settings(settings_path).spring_cursor,
                "Spring-centred pointer cannot be selected.");
        std::ofstream(settings_path) << "[Input]\nAnalogCursor=0\n";
        require(!read_settings(settings_path).analog_cursor,
                "Discrete stick navigation cannot be selected.");
        std::ofstream(settings_path) << "[Enhancements]\nSkipWorkstationLogin=1\n";
        require(read_settings(settings_path).skip_workstation_login,
                "The optional workstation skip setting was not read.");
        std::ofstream(settings_path) << "[Enhancements]\nMenuBlackBackground=0\n";
        require(!read_settings(settings_path).menu_black_background,
                "Original menu colours cannot be selected.");
        std::ofstream(settings_path) << "[Accessibility]\nCaptionFont=1\nCaptionScale=125\n";
        require(read_settings(settings_path).caption_style == CaptionStyle{CaptionFont::Game, 125},
                "Caption appearance settings were not read.");
        for (unsigned font = 2; font < caption_fonts.size(); ++font) {
            std::ofstream(settings_path) << "[Accessibility]\nCaptionFont=" << font << "\n";
            require(read_settings(settings_path).caption_style.font ==
                        static_cast<CaptionFont>(font),
                    "A bundled subtitle font was rejected.");
        }
        std::ofstream(settings_path) << "[Accessibility]\nCaptionFont=99\nCaptionScale=0\n";
        require(read_settings(settings_path).caption_style == CaptionStyle{},
                "Invalid caption settings did not fall back to readable defaults.");
        const auto path = directory / L"silence.mov";
        const auto data = fixture();
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(data.data()), data.size());
        output.close();
        invoke(Selector::QTMLInitInternals, 2);
        verify(path, audio_output);
        for (int index = audio_output ? 2 : 3; index < argc; ++index) {
            verify_external(argv[index]);
        }
        verify_last_frame(directory / L"video.mov");
        verify_last_frame(directory / L"captions.mov", true);
        verify_last_frame(directory / L"overlapping-captions.mov", true, true);
        verify_menu_black();
        require(playback::is_menu_entrance("49583.XMV") &&
                    playback::is_menu_entrance("49589.XMV") &&
                    !playback::is_menu_entrance("49585.XMV") &&
                    !playback::is_menu_entrance("19812.XMV"),
                "Menu entrance classification failed.");
        require(playback::normalize_caption(L" (  phone picked up  ) ") == L"(phone picked up)",
                "Caption parenthesis padding remains.");
        require(playback::normalize_caption(L"D.C.  . . .") == L"D.C. ...",
                "Ellipsis normalization damaged an abbreviation.");
        require(playback::normalize_caption(L"3.14  isn't\n a . b") == L"3.14 isn't\na . b",
                "Caption normalization damaged punctuation or line breaks.");
        invoke(Selector::QTMLTermInternals);
        FreeLibrary(library);
        std::cout << "Movie lifecycle, clock, seek, callbacks and rendering passed; audio output "
                  << (audio_output ? "enabled" : "disabled") << ".\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        invoke(Selector::QTMLTermInternals);
        FreeLibrary(library);
        return 1;
    }
}
