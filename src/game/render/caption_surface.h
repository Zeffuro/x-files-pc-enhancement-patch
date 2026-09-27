#pragma once
#include <windows.h>
#include <optional>

namespace native_game::caption_surface {
void paint(HDC dc, const RECT& overwritten, std::optional<RECT> caption = {}) noexcept;
void copy(HDC destination, const RECT& to, HDC source, const RECT& from, DWORD operation) noexcept;
void forget(HGDIOBJ bitmap);
void clear();
bool any();
int credit_offset(HDC dc, const RECT& credit, int top = 0) noexcept;
}
