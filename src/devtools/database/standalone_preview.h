#pragma once
#include "devtools/preview.h"
#include "image_view.h"
#include <windows.h>

namespace devtools::standalone {
inline constexpr wchar_t preview_class[] = L"XFilesStandaloneAssetPreview";
inline constexpr unsigned play_id = 4101, stop_id = 4102;
inline constexpr unsigned previous_id = 4103, next_id = 4104, frames_id = 4105, size_id = 4106;
inline constexpr unsigned tracks_id = 4107;

struct PreviewWindow {
    HWND window = nullptr, picture = nullptr, play = nullptr, stop = nullptr;
    HWND seek = nullptr, clock = nullptr, caption = nullptr;
    HWND previous = nullptr, next = nullptr, frames = nullptr, size = nullptr, tracks = nullptr;
    ImageView view;
    bool selecting = false;
    HMODULE module = nullptr;
    HFONT font = nullptr;
    std::unique_ptr<devtools::Preview> player;
    std::optional<std::size_t> painted_sample;
    std::optional<std::size_t> painted_track;
    bool first_tick = true;
};

LRESULT CALLBACK preview_proc(HWND window, UINT message, WPARAM value, LPARAM data);
void preview_tick(PreviewWindow& state);
}
