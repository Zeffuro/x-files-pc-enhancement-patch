#include "preferences/store.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

constexpr char preference_key[] = "Software\\Fox Interactive\\The X-Files\\Preferences";

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

HKEY open_preferences(REGSAM access = KEY_ALL_ACCESS) {
    HKEY software = nullptr;
    HKEY fox = nullptr;
    HKEY game = nullptr;
    HKEY preferences = nullptr;
    require(RegOpenKeyExA(HKEY_CURRENT_USER, "Software", 0, KEY_READ, &software) == ERROR_SUCCESS,
            "Opening the Software parent failed.");
    require(RegCreateKeyExA(software, "Fox Interactive", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                            &fox, nullptr) == ERROR_SUCCESS,
            "Opening the publisher parent failed.");
    RegCloseKey(software);
    require(RegOpenKeyExA(fox, "The X-Files", 0, KEY_ALL_ACCESS, &game) == ERROR_SUCCESS,
            "Opening the game parent failed.");
    RegCloseKey(fox);
    require(RegOpenKeyExA(game, "Preferences", 0, access, &preferences) == ERROR_SUCCESS,
            "Opening portable preferences failed.");
    RegCloseKey(game);
    return preferences;
}

DWORD WINAPI small_stack_thread(void*) {
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, preference_key, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return 1;
    }
    DWORD value = 0;
    DWORD size = sizeof(value);
    const auto error = RegQueryValueExA(key, "Volume Music", nullptr, nullptr,
                                        reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
    return error == ERROR_SUCCESS && value == 73 ? 0 : 1;
}

void verify_hooks() {
    const auto key = open_preferences();
    const DWORD value = 73;
    require(RegSetValueExA(key, "Volume Music", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                           sizeof(value)) == ERROR_SUCCESS,
            "Writing a portable DWORD failed.");
    DWORD size = 0;
    DWORD type = 0;
    require(RegQueryValueExA(key, "volume MUSIC", nullptr, &type, nullptr, &size) ==
                    ERROR_SUCCESS &&
                type == REG_DWORD && size == 4,
            "Case-insensitive size query failed.");
    DWORD read = 0xcccccccc;
    size = 1;
    require(RegQueryValueExA(key, "Volume Music", nullptr, &type, reinterpret_cast<BYTE*>(&read),
                             &size) == ERROR_MORE_DATA &&
                size == 4 && read == 0xcccccccc,
            "Short-buffer query changed data or failed to report its required size.");
    require(RegQueryValueExA(key, "Volume Music", nullptr, &type, reinterpret_cast<BYTE*>(&read),
                             &size) == ERROR_SUCCESS &&
                read == value,
            "The stored DWORD did not round-trip.");

    const char text[] = "  a \\\"quoted\\\" path  ";
    require(RegSetValueExA(key, "Game Save Name0", 0, REG_SZ, reinterpret_cast<const BYTE*>(text),
                           sizeof(text)) == ERROR_SUCCESS,
            "Writing a portable string failed.");
    char string[128]{};
    size = sizeof(string);
    require(RegQueryValueExA(key, "Game Save Name0", nullptr, &type,
                             reinterpret_cast<BYTE*>(string), &size) == ERROR_SUCCESS &&
                type == REG_SZ && std::string(string) == text && size == sizeof(text),
            "String contents or terminator length changed.");
    require(RegDeleteValueA(key, "Game Save Name0") == ERROR_SUCCESS &&
                RegDeleteValueA(key, "Game Save Name0") == ERROR_FILE_NOT_FOUND,
            "Deleting a portable value failed.");
    DWORD length = sizeof(string);
    require(RegEnumKeyExA(key, 0, string, &length, nullptr, nullptr, nullptr, nullptr) ==
                ERROR_NO_MORE_ITEMS,
            "Preferences unexpectedly exposed real subkeys.");
    require(RegDeleteKeyA(HKEY_CURRENT_USER, preference_key) == ERROR_ACCESS_DENIED,
            "Portable mode allowed deletion of the real preferences key.");
    HKEY child = nullptr;
    require(RegCreateKeyExA(HKEY_CURRENT_USER,
                            "Software\\Fox Interactive\\The X-Files\\Preferences\\test", 0, nullptr,
                            0, KEY_ALL_ACCESS, nullptr, &child, nullptr) == ERROR_ACCESS_DENIED,
            "Unsupported preference subkeys escaped to the real registry.");
    RegCloseKey(key);

    const auto read_only = open_preferences(KEY_READ);
    require(RegSetValueExA(read_only, "Volume Music", 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&value),
                           sizeof(value)) == ERROR_ACCESS_DENIED,
            "Read-only handle accepted a write.");
    RegCloseKey(read_only);

    HKEY unrelated = nullptr;
    require(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft", 0, KEY_READ, &unrelated) ==
                ERROR_SUCCESS,
            "An unrelated registry key was redirected.");
    RegCloseKey(unrelated);
}

void verify_display_preferences() {
    const auto key = open_preferences();
    for (const auto name : {"Auto Set Resolution", "Auto Set Depth"}) {
        const DWORD ask = 'A';
        require(RegSetValueExA(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&ask),
                               sizeof(ask)) == ERROR_SUCCESS,
                "Cannot set legacy display preference.");
        SetEnvironmentVariableW(L"XFILES_PATCH_DISPLAY", L"1");
        DWORD result = 0;
        DWORD size = sizeof(result);
        require(RegQueryValueExA(key, name, nullptr, nullptr, reinterpret_cast<BYTE*>(&result),
                                 &size) == ERROR_SUCCESS &&
                    result == DWORD(std::string(name) == "Auto Set Resolution" ? 'Y' : 'H'),
                "Patched launch did not select the virtual display path.");
        SetEnvironmentVariableW(L"XFILES_PATCH_DISPLAY", nullptr);
        require(RegQueryValueExA(key, name, nullptr, nullptr, reinterpret_cast<BYTE*>(&result),
                                 &size) == ERROR_SUCCESS &&
                    result == ask,
                "The display override changed the saved preference.");
    }
    RegCloseKey(key);
}

}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return 1;
    }
    HMODULE library = nullptr;
    try {
        const auto directory = std::filesystem::temp_directory_path() /
                               (L"xfiles-preferences-" + std::to_wstring(GetCurrentProcessId()) +
                                L"-" + std::to_wstring(GetTickCount64()));
        require(std::filesystem::create_directory(directory), "Cannot create test directory.");
        const auto path = directory / L"preferences.ini";
        {
            preferences::Store store(path);
            store.set("Volume Music", DWORD{40});
            store.set("Empty", std::string{});
            store.set("Path", std::string(R"(  C:\Game\"save"  )"));
        }
        require(SetEnvironmentVariableW(L"XFILES_PATCH_PREFERENCES", path.c_str()) != FALSE,
                "Cannot configure portable preferences.");
        library = LoadLibraryW(argv[1]);
        require(library != nullptr, "Cannot load QuickTime.qts with portable preferences.");
        verify_hooks();
        verify_display_preferences();
        Handle thread(CreateThread(nullptr, 65536, small_stack_thread, nullptr,
                                   STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr));
        require(thread.get() && WaitForSingleObject(thread.get(), 5000) == WAIT_OBJECT_0,
                "A thread with a legacy stack size could not run.");
        DWORD thread_result = 1;
        require(GetExitCodeThread(thread.get(), &thread_result) && thread_result == 0,
                "Preferences failed on a thread with a legacy stack size.");
        FreeLibrary(library);
        library = nullptr;
        {
            preferences::Store store(path);
            require(std::get<DWORD>(*store.find("volume music")) == 73 &&
                        std::get<std::string>(*store.find("Empty")).empty() &&
                        !store.find("Game Save Name0"),
                    "Preferences did not survive unloading the library.");
            bool locked = false;
            try {
                preferences::Store second(path);
            } catch (const std::system_error&) {
                locked = true;
            }
            require(locked, "A second writer acquired the preferences file.");
            require(std::get<std::string>(*store.find("Path")) == R"(  C:\Game\"save"  )",
                    "Quotes, backslashes or surrounding spaces did not survive saving.");
            require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
                    "Cannot prepare the failed-save test.");
            bool rejected = false;
            try {
                store.set("Volume Music", DWORD{12});
            } catch (const std::system_error&) {
                rejected = true;
            }
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            require(rejected && std::get<DWORD>(*store.find("Volume Music")) == 73,
                    "Failed saving changed the in-memory preferences.");
        }
        library = LoadLibraryW(argv[1]);
        require(library != nullptr, "Cannot reload portable preferences.");
        const auto key = open_preferences();
        DWORD read = 0;
        DWORD size = sizeof(read);
        require(RegQueryValueExA(key, "Volume Music", nullptr, nullptr,
                                 reinterpret_cast<BYTE*>(&read), &size) == ERROR_SUCCESS &&
                    read == 73,
                "Reloading did not restore the saved preferences.");
        RegCloseKey(key);
        FreeLibrary(library);
        library = nullptr;
        {
            std::ofstream damaged(path, std::ios::trunc);
            damaged << "[Game]\nVolume Music = not a number\n";
        }
        library = LoadLibraryW(argv[1]);
        require(library != nullptr, "Cannot load malformed-file test.");
        const auto invalid = open_preferences();
        require(RegQueryValueExA(invalid, "Volume Music", nullptr, nullptr,
                                 reinterpret_cast<BYTE*>(&read), &size) == ERROR_INVALID_DATA,
                "A malformed file silently fell back to the registry.");
        RegCloseKey(invalid);
        FreeLibrary(library);
        library = nullptr;
        SetEnvironmentVariableW(L"XFILES_PATCH_PREFERENCES", nullptr);
        std::cout
            << "Portable registry interception, persistence, buffers, locking and errors passed.\n";
        return 0;
    } catch (const std::exception& error) {
        if (library) {
            FreeLibrary(library);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
