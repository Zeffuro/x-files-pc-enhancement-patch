#pragma once

#include <filesystem>
#include <windows.h>

struct RunResult {
    DWORD exit_code;
    bool timed_out;
    bool desktop_changed;
    bool cursor_changed;
};

RunResult run_game(const std::filesystem::path& directory, bool portable, bool probe);
