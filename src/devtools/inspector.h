#pragma once

#include <windows.h>
#include <filesystem>

namespace devtools {
void request_inspector();
void request_easter_eggs();
void request_checkpoint(const std::filesystem::path& path);
void pump_inspector_messages();
void update_inspector(HWND game, bool available);
void release_inspector();
}
