#include "preferences.h"
#include "preferences/store.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

void import_registry(preferences::Store& store) {
    HKEY key = nullptr;
    const auto error =
        RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Fox Interactive\\The X-Files\\Preferences", 0,
                      KEY_QUERY_VALUE, &key);
    if (error == ERROR_FILE_NOT_FOUND) {
        return;
    }
    if (error != ERROR_SUCCESS) {
        throw std::runtime_error("Cannot read existing game preferences.");
    }

    struct Guard {
        HKEY key;

        ~Guard() {
            RegCloseKey(key);
        }
    } guard{key};

    std::vector<char> name(16384);
    std::vector<BYTE> data(65536);
    for (DWORD index = 0;; ++index) {
        DWORD name_size = static_cast<DWORD>(name.size());
        DWORD data_size = static_cast<DWORD>(data.size());
        DWORD type = 0;
        const auto status = RegEnumValueA(key, index, name.data(), &name_size, nullptr, &type,
                                          data.data(), &data_size);
        if (status == ERROR_NO_MORE_ITEMS) {
            return;
        }
        if (status != ERROR_SUCCESS) {
            throw std::runtime_error("Cannot import a game preference.");
        }
        if (type == REG_DWORD && data_size == sizeof(DWORD)) {
            DWORD number = 0;
            std::memcpy(&number, data.data(), sizeof(number));
            store.set(name.data(), number);
        } else if (type == REG_SZ && data_size && data[data_size - 1] == 0) {
            store.set(name.data(),
                      std::string(reinterpret_cast<const char*>(data.data()), data_size - 1));
        } else {
            throw std::runtime_error("An existing game preference has an unsupported type.");
        }
    }
}

}

void stage_preferences(const std::filesystem::path& source,
                       const std::filesystem::path& destination, bool import_existing) {
    const auto path = destination / L"preferences.ini";
    if (import_existing && std::filesystem::is_regular_file(source / L"preferences.ini")) {
        std::filesystem::copy_file(source / L"preferences.ini", path);
    }
    const bool imported = std::filesystem::exists(path);
    preferences::Store store(path);
    if (import_existing && !imported) {
        import_registry(store);
    }
    const std::pair<const char*, DWORD> defaults[] = {
        {"Auto Set Resolution", 'Y'},
        {"Auto Set Depth", 'H'},
        {"Install Level", 'M'},
        {"3D Sound", 1},
        {"ActionSceneDifficulty", 'S'},
        {"Artificial Intuition", 0},
        {"Auto Rewind", 1},
        {"Closed Captioning", 0},
        {"Flashlight Beam", 'W'},
        {"Flashlight Brightness", 'M'},
        {"High Quality Video", 1},
        {"Inventory", 'V'},
        {"Live Environments", 1},
        {"Previous Game", 0},
        {"Rollover Text", 1},
        {"Save Game On Exit", 'A'},
        {"Skip Clips", 0},
        {"Skip Intro", 0},
        {"Smooth Text", 1},
        {"Subliminal Messages", 1},
        {"Transitions", 1},
        {"Translucency", 1},
        {"Volume Dialog", 80},
        {"Volume Effect", 80},
        {"Volume Game", 70},
        {"Volume Music", 40},
    };
    // Missing legacy preferences are read as zero, including the logical display mode.
    for (const auto& [name, value] : defaults) {
        if (!store.find(name)) {
            store.set(name, value);
        }
    }
    store.set("Exe Full Path", (destination / L"XFiles.exe").string());
}
