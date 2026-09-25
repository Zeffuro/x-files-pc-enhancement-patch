#pragma once

#include <windows.h>
#include <array>
#include <cstddef>
#include <vector>

namespace enhancements {

struct Dialogue {
    std::array<RECT, 32> choices{};
    std::size_t count = 0;
    RECT talk{};
    RECT history{};
    bool is_history = false;
};

void attach_dialogue(HWND window);
void detach_dialogue();
void clear_dialogue();
void update_dialogue(bool focused);
bool close_dialogue(HWND window);
const Dialogue* current_dialogue();
bool navigate_dialogue(HWND window, int direction, int tab);
bool focus_conversation_evidence(HWND window);
std::vector<RECT> conversation_evidence();

}
