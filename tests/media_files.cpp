#include "dispatch.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cctype>

using namespace test;
namespace fs = std::filesystem;

__declspec(noinline) DWORD file_attributes(const char* path) {
    return GetFileAttributesA(path);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return 1;
    }
    HMODULE library = nullptr;
    try {
        const auto name = "xfiles-media-test-" + std::to_string(GetCurrentProcessId());
        const auto root =
            fs::temp_directory_path() / (name + "-" + std::to_string(GetTickCount64()));
        fs::create_directories(root / "MININST");
        std::ofstream(root / name) << "root";
        std::ofstream(root / "MININST" / (name + ".xmv")) << "movie";
        std::vector<wchar_t> module(32768);
        require(GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size())) != 0,
                "Cannot locate test executable.");
        const auto game = fs::path(module.data()).parent_path();
        const auto requested = (game / name).string();
        SetEnvironmentVariableW(L"XFILES_PATCH_PREFERENCES", nullptr);
        SetEnvironmentVariableW(L"XFILES_PATCH_MEDIA", root.c_str());
        library = LoadLibraryW(argv[1]);
        require(library != nullptr, "Cannot install local media hooks.");
        WIN32_FIND_DATAA data{};
        const auto found = FindFirstFileA(requested.c_str(), &data);
        require(found != INVALID_HANDLE_VALUE && data.nFileSizeLow == 4,
                "Game file lookup cannot see local media.");
        FindClose(found);
        auto lowercase = requested;
        std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        require(file_attributes(lowercase.c_str()) != INVALID_FILE_ATTRIBUTES,
                "Media mapping treated Windows paths as case-sensitive.");
        require(file_attributes((requested + ".xmv").c_str()) != INVALID_FILE_ATTRIBUTES,
                "DVD installation layer was not found.");
        const auto input = CreateFileA(requested.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, 0, nullptr);
        require(input != INVALID_HANDLE_VALUE, "Cannot open mapped media for reading.");
        char bytes[4]{};
        DWORD read = 0;
        const auto valid = ReadFile(input, bytes, sizeof(bytes), &read, nullptr);
        CloseHandle(input);
        require(valid && read == 4 && std::string(bytes, 4) == "root",
                "Wrong mapped file contents.");
        const auto write =
            CreateFileA(requested.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (write != INVALID_HANDLE_VALUE) {
            CloseHandle(write);
        }
        require(write == INVALID_HANDLE_VALUE, "A write was redirected into the source media.");
        const auto outside = (game / ".." / name).string();
        require(file_attributes(outside.c_str()) == INVALID_FILE_ATTRIBUTES,
                "An unrelated path escaped the game directory.");
        FreeLibrary(library);
        library = nullptr;
        require(file_attributes(requested.c_str()) == INVALID_FILE_ATTRIBUTES,
                "Media hooks were not restored on DLL unload.");
        std::cout << "Local media reads, DVD layers, path boundaries and hook cleanup passed.\n";
        return 0;
    } catch (const std::exception& error) {
        if (library) {
            FreeLibrary(library);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
