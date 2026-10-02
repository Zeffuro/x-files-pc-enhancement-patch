#pragma once
#include "native.h"
#include <windows.h>
#include <filesystem>
#include <functional>

namespace devtools {
HWND create_database_browser(HWND parent, HMODULE module, HFONT font,
                             const std::filesystem::path& root,
                             std::function<void(const std::filesystem::path&)> open_asset = {},
                             std::function<NativeDatabaseSnapshot()> snapshot_provider = {},
                             const std::filesystem::path& hdb_path = {}, bool asset_view = false,
                             const std::filesystem::path& selected_asset = {});
void update_database_browser(HWND window);
}
