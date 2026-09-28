#pragma once

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

namespace platform {
namespace font_detail {

class Registry {
    std::mutex mutex;
    std::map<std::filesystem::path, HANDLE> loaded;

public:
    ~Registry() {
        for (const auto& [path, memory] : loaded) {
            if (memory) {
                RemoveFontMemResourceEx(memory);
            } else {
                RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
            }
        }
    }

    bool add(const std::filesystem::path& path) {
        std::lock_guard lock(mutex);
        if (loaded.contains(path)) {
            return true;
        }
        if (AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr)) {
            loaded.emplace(path, nullptr);
            return true;
        }
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const auto size = input.tellg();
        if (size <= 0 || size > 4 * 1024 * 1024) {
            return false;
        }
        std::vector<char> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
            return false;
        }
        // Wine can reject the original font file while accepting the same bytes in memory.
        DWORD count = 0;
        const auto memory =
            AddFontMemResourceEx(bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, &count);
        if (!memory || !count) {
            if (memory) {
                RemoveFontMemResourceEx(memory);
            }
            return false;
        }
        loaded.emplace(path, memory);
        return true;
    }
};

}

inline bool register_private_font(const std::filesystem::path& path) {
    static font_detail::Registry registry;
    return registry.add(path);
}

inline bool register_game_font(const wchar_t* filename) {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) {
        return false;
    }
    path.resize(length);
    return register_private_font(std::filesystem::path(path).parent_path() / filename);
}

inline void register_game_fonts() {
    static const bool registered = [] {
        for (const auto filename : {L"DLG.TTR", L"HCD.TTR", L"JRN.TTR", L"PHN.TTR"}) {
            register_game_font(filename);
        }
        return true;
    }();
    (void)registered;
}

}
