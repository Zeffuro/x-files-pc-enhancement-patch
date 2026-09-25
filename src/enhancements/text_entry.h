#pragma once

#include <windows.h>
#include <string>

namespace enhancements {

void open_text_entry(HWND owner, const RECT& field, unsigned resource);
bool text_entry_busy();
void update_text_entry(HWND owner, bool focused);
bool text_entry_message(UINT message, WPARAM value, LPARAM data);
void release_text_entry();

}
