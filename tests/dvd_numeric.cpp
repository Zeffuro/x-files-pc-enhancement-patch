#include <windows.h>
#include "playback/fast_forward.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <typename Result, typename... Args>
Result call(void* object, unsigned slot, Args... args) {
    auto** table = *static_cast<void***>(object);
    return reinterpret_cast<Result(__thiscall*)(void*, Args...)>(table[slot])(object, args...);
}

void pump(DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}

template <typename Predicate> bool wait_for(DWORD milliseconds, Predicate predicate) {
    const auto end = GetTickCount64() + milliseconds;
    while (!predicate() && GetTickCount64() < end) {
        pump(10);
    }
    return predicate();
}

std::wstring caption(HWND parent) {
    wchar_t text[256]{};
    GetWindowTextW(parent, text, 256);
    return text;
}

struct SettingsGuard {
    std::filesystem::path path;
    bool existed;
    std::string original;

    explicit SettingsGuard(const std::filesystem::path& executable)
        : path(executable.parent_path() / L"patch.ini"), existed(std::filesystem::exists(path)) {
        if (existed) {
            std::ifstream stream(path, std::ios::binary);
            require(static_cast<bool>(stream), "Cannot preserve test settings");
            original.assign(std::istreambuf_iterator<char>(stream), {});
        }
        require(WritePrivateProfileStringW(L"Video", L"DVDMovies", L"1", path.c_str()) != 0,
                "Cannot enable DVD test playback");
        require(WritePrivateProfileStringW(L"Input", L"MovieSpeedKey", L"192", path.c_str()) != 0,
                "Cannot set test speed key");
        speed(2, true);
    }

    ~SettingsGuard() {
        if (existed) {
            std::ofstream(path, std::ios::binary | std::ios::trunc) << original;
        } else {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    }

    void mode(const wchar_t* value) {
        require(WritePrivateProfileStringW(L"Accessibility", L"Captions", value, path.c_str()) != 0,
                "Cannot set test caption mode");
    }

    void speed(unsigned multiplier, bool muted) {
        require(WritePrivateProfileStringW(L"Video", L"MovieSpeed",
                                           std::to_wstring(multiplier).c_str(), path.c_str()) &&
                    WritePrivateProfileStringW(L"Audio", L"MovieSpeedMute", muted ? L"1" : L"0",
                                               path.c_str()),
                "Cannot set test speed mode");
    }
};

void verify_shared_input(void* object, HWND parent, char* file, SettingsGuard& settings,
                         HMODULE quicktime) {
    const auto shared = reinterpret_cast<playback::FastForwardInput*(__cdecl*)()>(
        GetProcAddress(quicktime, "XFilesMovieSpeedInputV1"));
    require(shared != nullptr, "QuickTime does not expose shared movie-speed input");
    playback::HeldFastForward previous(*shared()), next(*shared());
    previous.update(false, true);
    require(previous.update(true, true), "Cannot prepare the QuickTime speed hold");
    settings.mode(L"1");
    require(call<int>(object, 9, static_cast<void*>(parent), file) == 1,
            "Cannot open DVD after the QuickTime speed hold");
    call<void>(object, 23, 480L);
    SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", L"1");
    call<void>(object, 10, static_cast<void*>(parent));
    pump(40);
    require(!GetPropW(parent, L"XFilesDvdTestAudio") &&
                reinterpret_cast<std::uintptr_t>(GetPropW(parent, L"XFilesDvdTestSpeed")) == 2,
            "DVD did not inherit QuickTime's continuous speed hold");
    require(wait_for(2000, [&] { return !call<int>(object, 20); }) && next.update(true, true),
            "DVD completion discarded the hold needed by the next QuickTime clip");
    call<void>(object, 8, 1);
    settings.mode(L"0");
    require(!call<int>(object, 9, static_cast<void*>(parent), file) && next.update(true, true),
            "Normal QuickTime fallback discarded the continuous speed hold");
    settings.mode(L"1");
    require(call<int>(object, 9, static_cast<void*>(parent), file) == 1,
            "Cannot open DVD for the playback error barrier");
    call<void>(object, 10, static_cast<void*>(parent));
    call<void>(object, 13, 99, 0);
    require(!call<int>(object, 20) && !next.update(true, true),
            "DVD playback failure retained the speed hold for the next clip");
    call<void>(object, 8, 1);
    ++shared()->context_epoch;
    require(call<int>(object, 9, static_cast<void*>(parent), file) == 1,
            "Cannot reopen DVD after a QuickTime focus barrier");
    call<void>(object, 23, 300L);
    call<void>(object, 10, static_cast<void*>(parent));
    pump(40);
    require(GetPropW(parent, L"XFilesDvdTestAudio") &&
                reinterpret_cast<std::uintptr_t>(GetPropW(parent, L"XFilesDvdTestSpeed")) == 1 &&
                !next.active(),
            "QuickTime's focus barrier did not reset both playback backends");
    SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr);
    call<void>(object, 12);
    call<void>(object, 8, 1);
}

void verify_binocular_fallback(void* object, HWND parent, char* file, SettingsGuard& settings) {
    for (const auto mode : {L"0", L"1", L"2"}) {
        settings.mode(mode);
        require(!call<int>(object, 9, static_cast<void*>(parent), file) && !call<int>(object, 20) &&
                    !call<void*>(object, 22),
                "Binocular clip bypassed QuickTime's native composition");
        require(std::string(call<char*>(object, 25)).find("QuickTime fallback") !=
                    std::string::npos,
                "Binocular fallback did not report its native playback path");
    }
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        namespace fs = std::filesystem;
        require(argc == 3 || argc == 4,
                "Expected fake-output adapter DLL, a proven numeric VOB and optional QuickTime");
        wchar_t executable[32768]{};
        require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "Cannot locate test binary");
        SettingsGuard settings(executable);
        const auto quicktime = argc == 4 ? LoadLibraryW(argv[3]) : nullptr;
        require(argc != 4 || quicktime, "Cannot load QuickTime for shared speed input checks");
        const auto module = LoadLibraryW(argv[1]);
        require(module != nullptr, "Cannot load test adapter");
        auto factory =
            reinterpret_cast<void*(__cdecl*)()>(GetProcAddress(module, "DLGetInterface"));
        auto tools_pause =
            reinterpret_cast<void(__cdecl*)(int)>(GetProcAddress(module, "XFilesSetToolsPaused"));
        require(factory && tools_pause, "Missing adapter exports");
        const auto parent = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 640, 480, nullptr, nullptr,
                                            nullptr, nullptr);
        require(parent != nullptr, "Cannot create hidden test parent");
        auto file = fs::path(argv[2]).string();
        OFSTRUCT info{};
        require(OpenFile(file.c_str(), &info, OF_EXIST) != HFILE_ERROR,
                "Cannot locate native movie input");
        auto* object = factory();
        require(object != nullptr, "Cannot create native interface");
        if (!_wcsicmp(fs::path(argv[2]).filename().c_str(), L"71914.vob")) {
            verify_binocular_fallback(object, parent, file.data(), settings);
            call<void*>(object, 0, 1u);
            DestroyWindow(parent);
            FreeLibrary(module);
            std::cout << "Binocular71914 QuickTime fallback passed in every caption mode\n";
            return 0;
        }
        if (quicktime) {
            verify_shared_input(object, parent, file.data(), settings, quicktime);
        }
        settings.mode(L"0");
        require(call<int>(object, 9, static_cast<void*>(parent), file.data()) == 0,
                "Unknown game caption state bypassed QuickTime fallback");
        settings.mode(L"1");
        require(call<int>(object, 9, static_cast<void*>(parent), file.data()) == 1,
                "Proven numeric pair did not open with captions On");
        call<void>(object, 23, 300L);
        call<void>(object, 10, static_cast<void*>(parent));
        require(caption(parent) == L"Yeah. It\u2019s him.", "Numeric play lost native first cue");
        call<void>(object, 11);
        pump(200);
        require(caption(parent) == L"Yeah. It\u2019s him." && !call<int>(object, 20),
                "Pause did not retain the current cue");
        call<void>(object, 13, 13, 420);
        require(caption(parent) == L"Thank you." && !call<int>(object, 20),
                "Paused seek did not select the second native cue");
        call<void>(object, 13, 13, 250);
        require(caption(parent).empty(), "Seek before the cue retained stale text");
        call<void>(object, 10, static_cast<void*>(parent));
        pump(1100);
        require(caption(parent) == L"Yeah. It\u2019s him.",
                "Audio-clock playback did not reach the first cue");
        tools_pause(1);
        pump(200);
        require(call<int>(object, 20) && caption(parent) == L"Yeah. It\u2019s him.",
                "Tools pause lost logical playback or its caption");
        settings.mode(L"2");
        tools_pause(0);
        require(caption(parent).empty(), "Captions Off on modal resume retained text");
        call<void>(object, 11);
        settings.mode(L"1");
        call<void>(object, 10, static_cast<void*>(parent));
        require(caption(parent) == L"Yeah. It\u2019s him.",
                "Captions On on resume did not restore the current cue");
        call<void>(object, 13, 13, 480);
        call<void>(object, 10, static_cast<void*>(parent));
        pump(1500);
        require(!call<int>(object, 20) && !call<int>(object, 21) && caption(parent).empty() &&
                    *call<char*>(object, 25) == '\0',
                "Numeric natural completion retained caption or became skip/failure");
        call<void>(object, 13, 13, 300);
        require(caption(parent) == L"Yeah. It\u2019s him.", "Seek after completion lost captions");
        call<void>(object, 12);
        require(call<int>(object, 21) && caption(parent).empty(), "Skip did not clear captions");
        settings.mode(L"2");
        require(call<int>(object, 9, static_cast<void*>(parent), file.data()) == 1,
                "Proven pair did not open with captions Off");
        call<void>(object, 13, 13, 300);
        require(caption(parent).empty(), "Captions Off displayed a paused-seek cue");
        call<void>(object, 10, static_cast<void*>(parent));
        pump(100);
        require(call<int>(object, 20) && caption(parent).empty(),
                "Captions Off changed playback or showed text");
        call<void>(object, 8, 1);
        require(caption(parent).empty(), "Close retained a caption");
        settings.mode(L"1");
        require(call<int>(object, 9, static_cast<void*>(parent), file.data()) == 1,
                "Cannot reopen numeric clip for speed checks");
        call<void>(object, 13, 13, 250);
        call<void>(object, 10, static_cast<void*>(parent));
        pump(40);
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", L"1");
        require(wait_for(2000, [&] { return !GetPropW(parent, L"XFilesDvdTestAudio"); }),
                "2x DVD playback did not mute audio");
        pump(550);
        require(!GetPropW(parent, L"XFilesDvdTestAudio"), "2x DVD playback did not mute audio");
        require(caption(parent) == L"Yeah. It\u2019s him.",
                "2x DVD playback did not reach the audio-aligned cue");
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr);
        pump(40);
        require(GetPropW(parent, L"XFilesDvdTestAudio") &&
                    caption(parent) == L"Yeah. It\u2019s him.",
                "Speed release did not resume audio at the current caption time");
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", L"1");
        pump(40);
        require(!GetPropW(parent, L"XFilesDvdTestAudio"), "Speed press did not discard queued PCM");
        SendMessageW(parent, WM_KILLFOCUS, 0, 0);
        Sleep(2000);
        SendMessageW(parent, WM_SETFOCUS, 0, 0);
        pump(40);
        require(GetPropW(parent, L"XFilesDvdTestAudio") &&
                    caption(parent) == L"Yeah. It\u2019s him.",
                "A focus gap between polls advanced DVD acceleration or left audio muted");
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr);
        pump(40);
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", L"1");
        pump(40);
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_UNFOCUSED", L"1");
        pump(40);
        require(GetPropW(parent, L"XFilesDvdTestAudio"), "Focus loss left DVD audio muted");
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_UNFOCUSED", nullptr);
        pump(40);
        require(GetPropW(parent, L"XFilesDvdTestAudio"), "Held key reactivated after focus return");
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr);
        pump(40);
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", L"1");
        pump(40);
        tools_pause(1);
        pump(200);
        tools_pause(0);
        pump(40);
        require(GetPropW(parent, L"XFilesDvdTestAudio"),
                "Modal return resumed a held acceleration");
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr);
        pump(40);
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", L"1");
        pump(40);
        call<void>(object, 11);
        call<void>(object, 10, static_cast<void*>(parent));
        pump(40);
        require(GetPropW(parent, L"XFilesDvdTestAudio"), "Native pause resumed held acceleration");
        call<void>(object, 13, 13, 480);
        call<void>(object, 10, static_cast<void*>(parent));
        pump(40);
        require(GetPropW(parent, L"XFilesDvdTestAudio"), "Seek did not reset held acceleration");
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr);
        pump(40);
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", L"1");
        require(wait_for(2000, [&] { return !call<int>(object, 20); }) && !call<int>(object, 21) &&
                    caption(parent).empty() && *call<char*>(object, 25) == '\0',
                "Accelerated EOF lost completion or caption clearing");
        call<void>(object, 8, 1);
        require(call<int>(object, 9, static_cast<void*>(parent), file.data()) == 1,
                "Cannot open the next clip during a continuous speed hold");
        call<void>(object, 10, static_cast<void*>(parent));
        pump(40);
        require(!GetPropW(parent, L"XFilesDvdTestAudio") &&
                    reinterpret_cast<std::uintptr_t>(GetPropW(parent, L"XFilesDvdTestSpeed")) == 2,
                "Natural completion cancelled acceleration in the next clip");
        call<void>(object, 12);
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr);
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_DRAIN_PENDING", L"1");
        call<void>(object, 23, 300L);
        call<void>(object, 24, 303L);
        call<void>(object, 10, static_cast<void*>(parent));
        pump(160);
        require(call<int>(object, 20), "Finite DVD range did not wait for pending audio");
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", L"1");
        pump(40);
        require(call<int>(object, 20) && *call<char*>(object, 25) == '\0',
                "Endpoint acceleration restarted an invalid finite DVD range");
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_DRAIN_PENDING", nullptr);
        pump(40);
        require(!call<int>(object, 20) && !call<int>(object, 21) &&
                    *call<char*>(object, 25) == '\0',
                "Finite DVD range did not complete after pending audio drained");
        call<void>(object, 8, 1);
        SendMessageW(parent, WM_KILLFOCUS, 0, 0);
        SendMessageW(parent, WM_SETFOCUS, 0, 0);
        require(call<int>(object, 9, static_cast<void*>(parent), file.data()) == 1,
                "Cannot reopen after acceleration");
        call<void>(object, 10, static_cast<void*>(parent));
        pump(40);
        require(GetPropW(parent, L"XFilesDvdTestAudio"),
                "Focus loss between clips retained held acceleration");
        call<void>(object, 12);
        require(call<int>(object, 21) && caption(parent).empty(),
                "Accelerated session skip failed");
        SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr);
        for (unsigned speed = 2; speed <= 4; ++speed) {
            for (const bool muted : {false, true}) {
                settings.speed(speed, muted);
                call<void>(object, 24, -1L);
                call<void>(object, 13, 13, 250);
                call<void>(object, 10, static_cast<void*>(parent));
                pump(40);
                SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", L"1");
                require(wait_for(2000,
                                 [&] {
                                     return reinterpret_cast<std::uintptr_t>(
                                                GetPropW(parent, L"XFilesDvdTestSpeed")) == speed;
                                 }),
                        "Configured DVD speed did not reach the audio output");
                pump(1000 / speed);
                require(caption(parent) == L"Yeah. It\u2019s him." &&
                            (GetPropW(parent, L"XFilesDvdTestAudio") != nullptr) == !muted,
                        "Configured speed lost audio/caption synchronization or mute policy");
                SendMessageW(parent, WM_KILLFOCUS, 0, 0);
                Sleep(300);
                SendMessageW(parent, WM_SETFOCUS, 0, 0);
                pump(40);
                require(GetPropW(parent, L"XFilesDvdTestAudio") &&
                            reinterpret_cast<std::uintptr_t>(
                                GetPropW(parent, L"XFilesDvdTestSpeed")) == 1 &&
                            caption(parent) == L"Yeah. It\u2019s him.",
                        "Focus transition retained accelerated audio or advanced the hidden gap");
                SetEnvironmentVariableW(L"XFILES_DVD_TEST_SPEED_DOWN", nullptr);
                call<void>(object, 12);
            }
        }
        call<void*>(object, 0, 1u);
        DestroyWindow(parent);
        FreeLibrary(module);
        if (quicktime) {
            FreeLibrary(quicktime);
        }
        std::cout << "Numeric DVD captions, lifecycle and muted/audible 2x/3x/4x speed passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
