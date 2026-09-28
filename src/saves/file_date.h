#pragma once

#include <windows.h>

#include <array>
#include <cwchar>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace saves {
inline std::wstring formatted_modified_date(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    FILETIME local{};
    SYSTEMTIME parts{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) ||
        !FileTimeToLocalFileTime(&attributes.ftLastWriteTime, &local) ||
        !FileTimeToSystemTime(&local, &parts)) {
        throw std::filesystem::filesystem_error(
            "Cannot read the saved game's date", path,
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    }
    std::array<wchar_t, 32> text{};
    if (swprintf_s(text.data(), text.size(), L"%04u-%02u-%02u %02u:%02u",
                   static_cast<unsigned>(parts.wYear), static_cast<unsigned>(parts.wMonth),
                   static_cast<unsigned>(parts.wDay), static_cast<unsigned>(parts.wHour),
                   static_cast<unsigned>(parts.wMinute)) < 0) {
        throw std::runtime_error("Cannot format the saved game's date");
    }
    return text.data();
}
}
