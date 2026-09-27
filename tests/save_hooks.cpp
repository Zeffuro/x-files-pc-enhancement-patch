#include "dispatch.h"

#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

__declspec(noinline) DWORD attributes(const char* path) {
    return GetFileAttributesA(path);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return 1;
    }
    HMODULE library = nullptr;
    try {
        const auto root = fs::temp_directory_path() /
                          (L"xfiles-save-hooks-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()));
        fs::create_directories(root / "saves");
        const auto original = root / "Slot.x";
        const auto target = root / "saves" / "Slot.x";
        std::ofstream(original) << "old";
        std::ofstream(target) << "newer";
        SetEnvironmentVariableW(L"XFILES_PATCH_PREFERENCES", nullptr);
        SetEnvironmentVariableW(L"XFILES_PATCH_MEDIA", nullptr);
        SetEnvironmentVariableW(L"XFILES_PATCH_SAVES", (root / "saves").c_str());
        library = LoadLibraryW(argv[1]);
        test::require(library != nullptr, "Cannot install native save hooks");
        OPENFILENAMEA invalid{};
        test::require(!GetOpenFileNameA(&invalid) && !GetSaveFileNameA(&invalid),
                      "Invalid native save dialog was accepted");
        const auto name = original.string();
        const auto input = CreateFileA(name.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, 0, nullptr);
        test::require(input != INVALID_HANDLE_VALUE, "Cannot read migrated native save");
        char bytes[5]{};
        DWORD read = 0;
        const auto valid = ReadFile(input, bytes, sizeof(bytes), &read, nullptr);
        CloseHandle(input);
        test::require(valid && read == 5 && std::string(bytes, 5) == "newer",
                      "Previous Game would read the old root save");
        WIN32_FIND_DATAA data{};
        const auto found = FindFirstFileA(name.c_str(), &data);
        test::require(found != INVALID_HANDLE_VALUE && data.nFileSizeLow == 5,
                      "Native save lookup cannot see migrated saves");
        FindClose(found);
        test::require(DeleteFileA(name.c_str()) && fs::exists(original) && !fs::exists(target),
                      "Replacing a native slot deleted the original backup");
        test::require(attributes(name.c_str()) == INVALID_FILE_ATTRIBUTES,
                      "Deleted save fell back to an old root copy");
        const auto output = CreateFileA(name.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        test::require(output != INVALID_HANDLE_VALUE, "Cannot write redirected native save");
        CloseHandle(output);
        test::require(fs::file_size(original) == 3 && fs::file_size(target) == 0,
                      "Native save overwrote the root backup");
        FreeLibrary(library);
        library = nullptr;
        test::require(attributes(name.c_str()) != INVALID_FILE_ATTRIBUTES,
                      "Native save hooks were not removed on unload");
        std::cout << "Native save reads, writes, replacement and lookup remain in saves.\n";
        return 0;
    } catch (const std::exception& error) {
        if (library) {
            FreeLibrary(library);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
