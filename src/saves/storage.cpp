#include "storage.h"
#include "header.h"

#include <windows.h>
#include <stdexcept>

namespace saves {
std::filesystem::path prepare_directory(const std::filesystem::path& game) {
    const auto directory = game / L"saves";
    std::filesystem::create_directory(directory);
    const auto attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        throw std::runtime_error(
            "The saves folder must be a regular folder inside the game folder");
    }
    const auto marker = directory / L".legacy-imported";
    if (std::filesystem::is_regular_file(marker)) {
        return directory;
    }
    for (const auto& entry : std::filesystem::directory_iterator(game)) {
        if (_wcsicmp(entry.path().extension().c_str(), L".x") || !entry.is_regular_file() ||
            !supported_header(entry.path())) {
            continue;
        }
        const auto target = directory / entry.path().filename();
        if (std::filesystem::exists(target)) {
            continue;
        }
        const auto temporary = directory / (entry.path().filename().wstring() + L".migrating");
        if (!CopyFileW(entry.path().c_str(), temporary.c_str(), TRUE)) {
            throw std::runtime_error("Cannot copy an existing saved game into the saves folder");
        }
        if (!MoveFileW(temporary.c_str(), target.c_str())) {
            const auto error = GetLastError();
            DeleteFileW(temporary.c_str());
            if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS) {
                throw std::runtime_error("Cannot finish copying an existing saved game");
            }
        }
    }
    std::ofstream complete(marker, std::ios::binary);
    complete << "1\n";
    complete.close();
    if (!complete) {
        throw std::runtime_error("Cannot record saved game migration");
    }
    return directory;
}
}
