#pragma once
#include "slots.h"
#include "catalog.h"
#include "preview.h"
#include "browser_text.h"
#include <windows.h>
#include <array>
#include <memory>

namespace saves {
struct Canvas {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    std::uint8_t* pixels = nullptr;
    Canvas();
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
};

struct Browser {
    ~Browser() {
        if (font) {
            DeleteObject(font);
        }
        if (action_font) {
            DeleteObject(action_font);
        }
    }

    std::filesystem::path root;
    BrowserText text;
    HFONT font = nullptr;
    HFONT action_font = nullptr;
    Canvas background, output, scratch;
    Catalog legacy;
    std::array<Slot, slots_per_page> slots{};
    std::array<Thumbnail, slots_per_page> thumbnails{};
    Thumbnail scene;
    SceneReference scene_reference;
    std::unique_ptr<ScenePreview> preview;
    int preview_slot = -1;
    ULONGLONG preview_started = 0;
    ULONGLONG preview_painted = 0;
    bool preview_failed = false;
    unsigned page = 0, selection = 0;
    int focus = 0, hover = -1;
    unsigned key = 0;
    bool keyboard = false, deleting = false;
    bool saving = false, existing = false, naming = false, confirm = false;
    std::wstring name, status;
};

RECT card_rect(unsigned index);
RECT control_rect(int item);
bool control_enabled(const Browser& state, int item);
void move_browser_focus(Browser& state, int horizontal, int vertical);
void cycle_browser_focus(Browser& state, int direction);
inline constexpr wchar_t name_keys[] = L"1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM,.?";
inline constexpr unsigned name_key_count = 44;
RECT name_key_rect(unsigned key);
void draw_browser(Browser& state);
void load_browser_art(Browser& state);
void load_browser_page(Browser& state);
Thumbnail capture_scene(HDC dc, const RECT& bounds);
}
