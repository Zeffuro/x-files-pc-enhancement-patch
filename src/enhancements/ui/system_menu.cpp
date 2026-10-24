#include "system_menu.h"

#include <utility>

namespace enhancements::system_menu {
bool refresh(HWND window, ui::Language language) {
    const auto menu = GetSystemMenu(window, FALSE);
    if (!menu) {
        return false;
    }
    const auto missing = [&](UINT id) { return GetMenuState(menu, id, MF_BYCOMMAND) == UINT(-1); };
    if (missing(settings_command) && missing(inspector_command)) {
        const auto count = GetMenuItemCount(menu);
        const auto last = count > 0 ? GetMenuState(menu, count - 1, MF_BYPOSITION) : 0;
        if (count > 0 && !(last & MF_SEPARATOR) && !AppendMenuW(menu, MF_SEPARATOR, 0, nullptr)) {
            return false;
        }
    }
    bool success = true;
    for (const auto& entry : {std::pair{settings_command, L"Enhancements...\tF10"},
                              std::pair{inspector_command, L"Developer tools...\tCtrl+F11"}}) {
        const auto label = ui::translate(entry.second, language);
        const auto changed =
            missing(entry.first)
                ? AppendMenuW(menu, MF_STRING, entry.first, label)
                : ModifyMenuW(menu, entry.first, MF_BYCOMMAND | MF_STRING, entry.first, label);
        success = changed && success;
    }
    return success;
}
}
