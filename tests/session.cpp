#include "launcher/session.h"
#include "launcher/preferences.h"
#include "preferences/store.h"
#include "dispatch.h"

#include <fstream>
#include <iostream>

namespace {

bool rejected(const std::filesystem::path& directory) {
    try {
        read_session(directory);
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

}

int main() {
    try {
        namespace fs = std::filesystem;
        using test::require;
        const auto directory = fs::temp_directory_path() /
                               (L"xfiles-session-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                std::to_wstring(GetTickCount64()));
        const auto media = directory / L"Disc files \u00e9\u65e5";
        fs::create_directories(media);
        require(rejected(directory), "Missing session settings were accepted.");
        save_session(directory, {media, true});
        require(rejected(directory), "Missing portable preferences silently used the registry.");
        std::ofstream(directory / L"preferences.ini") << "[Game]\n";
        const auto session = read_session(directory);
        require(session.portable && session.media == fs::canonical(media),
                "Session round trip lost Unicode paths or portable mode.");
        const auto path = directory / L"session.ini";
        require(WritePrivateProfileStringW(L"Session", L"Portable", L"invalid", path.c_str()),
                "Cannot create invalid session fixture.");
        require(rejected(directory), "Malformed preferences mode silently used the registry.");
        require(WritePrivateProfileStringW(L"Session", L"Portable", L"0", path.c_str()),
                "Cannot select registry mode.");
        require(!read_session(directory).portable, "Explicit registry mode was not retained.");
        const auto clean = directory / L"Clean";
        fs::create_directory(clean);
        std::ofstream(media / L"preferences.ini") << "not a valid preferences file";
        stage_preferences(media, clean, false);
        {
            preferences::Store defaults(clean / L"preferences.ini");
            require(std::get<DWORD>(*defaults.find("Auto Set Resolution")) == 'Y',
                    "Clean install did not initialize the game's logical display mode.");
            require(std::get<DWORD>(*defaults.find("Volume Dialog")) > 0,
                    "Clean install starts with silent dialogue.");
            require(!defaults.find("Game Save Name0"),
                    "Clean install imported existing save slots.");
            defaults.set("Volume Dialog", DWORD{23});
        }
        const auto imported = directory / L"Imported";
        fs::create_directory(imported);
        stage_preferences(clean, imported);
        {
            preferences::Store copied(imported / L"preferences.ini");
            require(std::get<DWORD>(*copied.find("Volume Dialog")) == 23,
                    "Setup replaced an existing preference with its default.");
        }
        std::cout << "Session settings and portable-mode validation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
