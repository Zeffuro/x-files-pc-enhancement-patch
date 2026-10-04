#pragma once

#include <windows.h>
#include <array>
#include <cstddef>
#include <vector>
#include <string>

namespace enhancements {

struct Dialogue {
    std::array<RECT, 32> choices{};
    std::array<std::wstring, 32> text{};
    std::size_t count = 0;
    RECT talk{};
    RECT history{};
    RECT panel{};
    RECT viewport{};
    bool is_history = false;
};

void attach_dialogue(HWND window);
void detach_dialogue();
void clear_dialogue();
void update_dialogue(bool focused);
bool close_dialogue(HWND window);
const Dialogue* current_dialogue();
bool navigate_dialogue(HWND window, int direction, int tab);
bool scroll_dialogue(int direction);
bool focus_conversation_evidence(HWND window);
std::vector<RECT> conversation_evidence();
void begin_dialogue_click(HWND window, UINT message);
void finish_dialogue_click(UINT message);
void cancel_dialogue_click();

}
