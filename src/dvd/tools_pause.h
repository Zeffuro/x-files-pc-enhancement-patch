#pragma once

#include <windows.h>

namespace dvd {
// Construct and destroy on the native movie's UI thread.
class ToolPause {
public:
    ToolPause() noexcept {
        if (GetModuleHandleExW(0, L"XFilesMpeg.dll", &module_)) {
            pause_ = reinterpret_cast<Pause>(GetProcAddress(module_, "XFilesSetToolsPaused"));
            if (pause_) {
                pause_(1);
            }
        }
    }

    ~ToolPause() {
        if (pause_) {
            pause_(0);
        }
        if (module_) {
            FreeLibrary(module_);
        }
    }

    ToolPause(const ToolPause&) = delete;
    ToolPause& operator=(const ToolPause&) = delete;

private:
    using Pause = void(__cdecl*)(int);
    HMODULE module_ = nullptr;
    Pause pause_ = nullptr;
};
}
