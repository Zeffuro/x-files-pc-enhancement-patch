#pragma once
#include <windows.h>

namespace enhancements {
void notify_status(HWND owner, const wchar_t* message);
void update_notification(HWND owner);
void release_notification();
}
