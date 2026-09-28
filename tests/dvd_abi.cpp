#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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

void pump(void* object, DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end && call<int>(object, 20)) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        namespace fs = std::filesystem;
        require(argc >= 3, "Expected adapter DLL and fixture");
        if (argc > 3 && std::wstring_view(argv[3]) == L"--disabled") {
            SetDllDirectoryW(fs::path(argv[1]).parent_path().c_str());
            const auto module = LoadLibraryW(argv[1]);
            require(module != nullptr, "Cannot load disabled DVD adapter");
            auto factory =
                reinterpret_cast<void*(__cdecl*)()>(GetProcAddress(module, "DLGetInterface"));
            require(factory != nullptr, "Missing DVD factory");
            auto* object = factory();
            require(object != nullptr, "Disabled DVD factory failed");
            const auto parent = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 640, 480, nullptr,
                                                nullptr, nullptr, nullptr);
            auto file = fs::path(argv[2]).string();
            require(call<int>(object, 9, static_cast<void*>(parent), file.data()) == 0,
                    "Disabled DVD playback accepted a startup movie");
            require(std::string_view(call<char*>(object, 25)) == "DVD clip uses QuickTime fallback",
                    "Disabled DVD playback failed for an unrelated reason");
            require(call<int>(object, 20) == 0 && call<int>(object, 21) == 0,
                    "Disabled DVD playback became a playing or skipped movie");
            call<void*>(object, 0, 1u);
            DestroyWindow(parent);
            FreeLibrary(module);
            return 0;
        }
        const auto root = fs::temp_directory_path() /
                          (L"xfiles-dvd-abi-" + std::to_wstring(GetCurrentProcessId()));
        fs::create_directory(root);

        struct Cleanup {
            fs::path path;

            ~Cleanup() {
                std::error_code ec;
                fs::remove_all(path, ec);
            }
        } cleanup{root};

        fs::copy_file(argv[2], root / L"teaser.vob");
        fs::copy_file(argv[2], root / L"19650.vob");
        auto file = (root / L"teaser.vob").string();
        auto captioned = (root / L"19650.vob").string();
        auto missing = (root / L"ddigital1.vob").string();
        const auto module = LoadLibraryW(argv[1]);
        require(module != nullptr, "Cannot load DVD adapter");
        auto factory =
            reinterpret_cast<void*(__cdecl*)()>(GetProcAddress(module, "DLGetInterface"));
        require(factory != nullptr, "Missing undecorated cdecl factory");
        auto tools_pause =
            reinterpret_cast<void(__cdecl*)(int)>(GetProcAddress(module, "XFilesSetToolsPaused"));
        require(tools_pause != nullptr, "Missing tools pause bridge");
        OFSTRUCT file_info{};
        constexpr const char* native_path = "\\vob\\dvd-hook-fixture.vob";
        require(OpenFile(native_path, &file_info, OF_EXIST) == HFILE_ERROR,
                "Unexpected native root fixture");
        const auto parent = CreateWindowExW(0, L"STATIC", L"DVD adapter test", WS_OVERLAPPEDWINDOW,
                                            0, 0, 660, 520, nullptr, nullptr, nullptr, nullptr);
        require(parent != nullptr, "Cannot create test parent");
        if (argc > 3) {
            ShowWindow(parent, SW_SHOW);
        }
        auto* object = factory();
        require(object != nullptr && call<int>(object, 1) == 1, "Factory/init failed");
        require(OpenFile(native_path, &file_info, OF_EXIST) != HFILE_ERROR,
                "Portable VOB existence hook failed");
        require(OpenFile(native_path, &file_info, OF_READ) == HFILE_ERROR,
                "VOB hook redirected unsupported flags");
        require(OpenFile("\\other\\dvd-hook-fixture.vob", &file_info, OF_EXIST) == HFILE_ERROR,
                "VOB hook redirected another folder");
        require(OpenFile("\\vob\\missing-fixture.vob", &file_info, OF_EXIST) == HFILE_ERROR,
                "VOB hook accepted missing media");
        call<void>(object, 3);
        require(call<int>(object, 9, static_cast<void*>(parent), captioned.data()) == 0,
                "Unmapped captioned clip bypassed fallback");
        require(call<int>(object, 9, static_cast<void*>(parent), missing.data()) == 0,
                "Missing input accepted");
        require(*call<char*>(object, 25) != '\0', "Open failure lost its error");
        require(call<int>(object, 9, static_cast<void*>(parent), file.data()) == 1, "Open failed");
        call<void>(object, 5, RECT{0, 0, 704, 480});
        call<void>(object, 5, RECT{0, 0, 64, 48});
        call<void>(object, 6, RECT{0, 0, 640, 480});
        call<void>(object, 7, RECT{0, 0, 640, 480});
        call<void>(object, 4, 0);
        require(call<int>(object, 15) == 1 && call<int>(object, 16) == 0,
                "Unexpected device/overlay ABI");
        require(call<long>(object, 19) == 0 && call<void*>(object, 22) != nullptr,
                "Invalid driver/window ABI");
        call<void>(object, 23, 0L);
        call<void>(object, 24, -1L);
        call<void>(object, 10, static_cast<void*>(parent));
        require(call<int>(object, 20) == 1, "Play failed");
        call<void>(object, 17, static_cast<void*>(parent), 1u);
        require(call<int>(object, 20) == 1, "Stale MCI notification completed new playback");
        pump(object, 250);
        tools_pause(1);
        tools_pause(1);
        auto* held = factory();
        require(held != nullptr && call<int>(held, 9, static_cast<void*>(parent), file.data()) == 1,
                "Cannot open movie inside tools pause");
        call<void>(held, 10, static_cast<void*>(parent));
        pump(object, 3500);
        require(call<int>(object, 20) == 1 && call<int>(held, 20) == 1,
                "Tools pause allowed a movie to finish");
        call<void>(held, 8, 1);
        call<void*>(held, 0, 1u);
        tools_pause(0);
        pump(object, 100);
        require(call<int>(object, 20) == 1, "Nested tools pause resumed too early");
        tools_pause(0);
        pump(object, 500);
        require(call<int>(object, 20) == 1 && *call<char*>(object, 25) == '\0',
                "Tools pause advanced the audio clock or failed to resume");
        Sleep(500);
        pump(object, 5000);
        require(call<int>(object, 20) == 0 && call<int>(object, 21) == 0,
                "EOF hung or became a user skip");
        require(*call<char*>(object, 25) == '\0', "Playback reported a failure");
        call<void>(object, 10, static_cast<void*>(parent));
        call<void>(object, 11);
        require(call<int>(object, 20) == 0, "Pause failed");
        tools_pause(1);
        tools_pause(0);
        require(call<int>(object, 20) == 0, "Tools pause resumed a previously paused movie");
        call<void>(object, 10, static_cast<void*>(parent));
        tools_pause(1);
        call<void>(object, 11);
        tools_pause(0);
        require(call<int>(object, 20) == 0, "Tools pause discarded a native pause request");
        tools_pause(1);
        call<void>(object, 10, static_cast<void*>(parent));
        require(call<int>(object, 20) == 1, "Play during tools pause lost logical playback");
        call<void>(object, 12);
        tools_pause(0);
        require(call<int>(object, 20) == 0 && call<int>(object, 21) == 1,
                "Tools pause resumed a stopped movie");
        call<void>(object, 10, static_cast<void*>(parent));
        require(call<int>(object, 18, static_cast<void*>(parent), static_cast<unsigned>(WM_KEYDOWN),
                          static_cast<unsigned>(VK_ESCAPE), 0L) == 1,
                "Escape handler failed");
        require(call<int>(object, 21) == 1 && call<int>(object, 20) == 0, "Skip failed");
        call<void>(object, 13, 11, 0);
        call<void>(object, 14, 1);
        call<void>(object, 13, 12, 0);
        require(call<int>(object, 20) == 0 && *call<char*>(object, 25) == '\0',
                "Seek-to-end failed");
        call<void>(object, 12);
        if (argc == 3) {
            SetEnvironmentVariableW(L"XFILES_DVD_TEST_UNDERRUN", L"1");
            require(call<int>(object, 9, static_cast<void*>(parent), file.data()) == 1,
                    "Underrun fixture open failed");
            SetEnvironmentVariableW(L"XFILES_DVD_TEST_UNDERRUN", nullptr);
            call<void>(object, 10, static_cast<void*>(parent));
            pump(object, 500);
            require(call<int>(object, 20) == 0 && call<int>(object, 21) == 0 &&
                        *call<char*>(object, 25) != '\0',
                    "Audio starvation hung or became successful EOF");
        }
        call<void>(object, 8, 1);
        call<void>(object, 2);
        call<void*>(object, 0, 1u);
        require(OpenFile(native_path, &file_info, OF_EXIST) == HFILE_ERROR,
                "DVD deletion did not restore OpenFile");
        DestroyWindow(parent);
        require(FreeLibrary(module) != 0, "Adapter unload failed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
