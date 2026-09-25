#pragma once

#include <windows.h>

namespace enhancements {
bool navigate_emotions(HWND window, int direction, bool focus);

bool navigate_screen(HWND window, int horizontal, int vertical, bool activate, bool cancel,
                     bool change_group, bool keyboard = false);

}
