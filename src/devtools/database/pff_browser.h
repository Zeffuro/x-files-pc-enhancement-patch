#pragma once
#include <windows.h>
#include <filesystem>
#include <memory>

namespace devtools {
inline constexpr wchar_t pff_browser_class[] = L"XFilesPffBrowser";
std::shared_ptr<HWND> open_pff_browser(HWND owner, HMODULE module, HFONT font,
                                       const std::filesystem::path& path, bool visible = true);
}
