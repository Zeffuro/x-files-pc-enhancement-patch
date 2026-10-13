#pragma once

#include <windows.h>

namespace dvd {
inline bool movie_playing() noexcept {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(0, L"XFilesMpeg.dll", &module)) {
        return false;
    }
    using Query = int(__cdecl*)() noexcept;
    const auto query = reinterpret_cast<Query>(GetProcAddress(module, "XFilesMoviePlaying"));
    const bool playing = !query || query() != 0;
    FreeLibrary(module);
    return playing;
}
}
