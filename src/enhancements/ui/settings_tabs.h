#pragma once

#include <windows.h>
#include "localization/ui.h"
#include <array>
#include <vector>

namespace enhancements {

class SettingsTabs {
public:
    void initialize(HWND dialog, ui::Language language = ui::language());
    void select(unsigned page);

private:
    static LRESULT CALLBACK keyboard(HWND window, UINT message, WPARAM parameter, LPARAM data,
                                     UINT_PTR id, DWORD_PTR reference);
    HWND tab_ = nullptr;
    std::array<std::vector<HWND>, 4> pages_;
};

}
