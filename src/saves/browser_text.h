#pragma once

#include <windows.h>
#include <filesystem>
#include <string>

namespace saves {
struct BrowserText {
    std::wstring previous, next, name, empty, slot, remove, confirm_delete, overwrite;
    std::wstring overwrite_prompt, load_warning, done, clear, space, backspace;
    std::wstring existing, numbered, page, saved, save, load, cancel;
    std::wstring unreadable, no_preview, keep_playing, load_now;
    std::wstring quicksave, autosaves;
};

BrowserText load_browser_text(const std::filesystem::path& game);
HFONT create_browser_font(const std::filesystem::path& game, int height);
}
