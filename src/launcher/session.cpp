#include "session.h"

#include <windows.h>
#include <fstream>
#include <stdexcept>
#include <string>

void save_session(const std::filesystem::path& directory, const Session& session) {
    const auto path = directory / L"session.ini";
    if (std::filesystem::exists(path)) {
        throw std::runtime_error("Session settings already exist.");
    }
    std::ofstream output(path, std::ios::binary);
    output.write("\xff\xfe", 2);
    output.close();
    const auto media =
        std::filesystem::canonical(session.media) == std::filesystem::canonical(directory)
            ? std::filesystem::path(L".")
            : std::filesystem::canonical(session.media);
    if (!output || !WritePrivateProfileStringW(L"Session", L"Media", media.c_str(), path.c_str()) ||
        !WritePrivateProfileStringW(L"Session", L"Portable", session.portable ? L"1" : L"0",
                                    path.c_str())) {
        throw std::runtime_error("Cannot save session settings.");
    }
}

Session read_session(const std::filesystem::path& directory) {
    const auto path = directory / L"session.ini";
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("This folder has not been set up. Run XFilesSetup.exe first.");
    }
    std::wstring media(32768, L'\0');
    const auto length = GetPrivateProfileStringW(L"Session", L"Media", L"", media.data(),
                                                 static_cast<DWORD>(media.size()), path.c_str());
    wchar_t portable[3]{};
    const auto flag_length = GetPrivateProfileStringW(L"Session", L"Portable", L"", portable,
                                                      _countof(portable), path.c_str());
    if (!length || length >= media.size() - 1 || flag_length != 1 ||
        (portable[0] != L'0' && portable[0] != L'1')) {
        throw std::runtime_error("Session settings are invalid.");
    }
    media.resize(length);
    const bool local = portable[0] == L'1';
    if (local && !std::filesystem::is_regular_file(directory / L"preferences.ini")) {
        throw std::runtime_error(
            "The game preferences are missing. Restore preferences.ini from a backup.");
    }
    const std::filesystem::path root(media);
    return {std::filesystem::canonical(root.is_absolute() ? root : directory / root), local};
}
